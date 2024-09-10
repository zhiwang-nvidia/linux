// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 *
 */

#include <cxl/cxl.h>
#include <cxl/pci.h>
#include <linux/pci.h>

#define BUFFER_SIZE (SZ_256M)

struct qemu_cxl_accel {
	struct cxl_dev_state *cxlds;
	struct cxl_memdev *cxlmd;
	struct cxl_root_decoder *cxlrd;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_region *region;
};

static int qemu_cxl_accel_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	DECLARE_BITMAP(expected, CXL_MAX_CAPS);
	DECLARE_BITMAP(found, CXL_MAX_CAPS);
	resource_size_t max = 0;
	struct qemu_cxl_accel *cxl;
	struct resource res;
	struct range range;
	u16 dvsec;
	int ret;

	dvsec = pci_find_dvsec_capability(pdev, PCI_VENDOR_ID_CXL,
					  CXL_DVSEC_PCIE_DEVICE);
	if (!dvsec)
		return -ENODEV;

	cxl = kzalloc(sizeof(*cxl), GFP_KERNEL);
	if (!cxl)
		return -ENOMEM;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;

	cxl->cxlds = cxl_accel_state_create(&pdev->dev);
	if (IS_ERR(cxl->cxlds)) {
		pci_err(pdev, "fail to create CXL accel device state\n");
		ret = PTR_ERR(cxl->cxlds);
		goto err_state;
	}

	cxl_set_dvsec(cxl->cxlds, dvsec);
	cxl_set_serial(cxl->cxlds, pdev->dev.id);

	res = DEFINE_RES_MEM(0, BUFFER_SIZE);
	if (cxl_set_resource(cxl->cxlds, res, CXL_RES_DPA)) {
		pci_err(pdev, "cxl_set_resource DPA failed\n");
		ret = -EINVAL;
		goto err_resource_set;
	}

	res = DEFINE_RES_MEM_NAMED(0, BUFFER_SIZE, "ram");
	if (cxl_set_resource(cxl->cxlds, res, CXL_RES_RAM)) {
		pci_err(pdev, "cxl_set_resource RAM failed\n");
		ret = -EINVAL;
		goto err_resource_set;
	}

	ret = cxl_pci_accel_setup_regs(pdev, cxl->cxlds);
	if (ret) {
		pci_err(pdev, "CXL accel setup regs failed\n");
		goto err_resource_set;
	}

	bitmap_clear(expected, 0, CXL_MAX_CAPS);
	bitmap_set(expected, CXL_DEV_CAP_HDM, 1);

	if (!cxl_pci_check_caps(cxl->cxlds, expected, found)) {
		pci_err(pdev,
			"CXL device capabilities found(%08lx) not as expected(%08lx)\n",
			*found, *expected);
		ret = -EIO;
		goto err_resource_set;
	}

	ret = cxl_request_resource(cxl->cxlds, CXL_RES_RAM);
	if (ret) {
		pci_err(pdev, "CXL request resource failed\n");
		goto err_resource_set;
	}

	ret = cxl_await_range_active(cxl->cxlds);
	if (ret) {
		pci_err(pdev, "CXL accel media not active\n");
		goto err_media_ready;
	}

	cxl_set_media_ready(cxl->cxlds);

	cxl->cxlmd = devm_cxl_add_memdev(&pdev->dev, cxl->cxlds);
	if (IS_ERR(cxl->cxlmd)) {
		pci_err(pdev, "CXL accel memdev creation failed\n");
		ret = PTR_ERR(cxl->cxlmd);
		goto err_memdev;
	}

	cxl->cxlrd = cxl_get_hpa_freespace(cxl->cxlmd,
					   CXL_DECODER_F_RAM | CXL_DECODER_F_TYPE2,
					   &max);
	if (IS_ERR(cxl->cxlrd)) {
		pci_err(pdev, "cxl_get_hpa_freespace failed\n");
		ret = PTR_ERR(cxl->cxlrd);
		goto err_memdev;
	}

	if (max < BUFFER_SIZE) {
		pci_err(pdev, "No enough free HPA space %llu < %u\n",
			max,BUFFER_SIZE);
		ret = -ENOSPC;
		goto err_memdev;
	}

	cxl->cxled = cxl_request_dpa(cxl->cxlmd, true, BUFFER_SIZE,
				     BUFFER_SIZE);
	if (IS_ERR(cxl->cxled)) {
		pci_err(pdev, "CXL accel request DPA failed\n");
		ret = PTR_ERR(cxl->cxled);
		goto err_memdev;
	}

	cxl->region = cxl_create_region(cxl->cxlrd, cxl->cxled, true);
	if (!cxl->region) {
		pci_err(pdev, "CXL accel create region failed\n");
		ret = PTR_ERR(cxl->region);
		goto err_region;
	}

	ret = cxl_get_region_range(cxl->region, &range);
	if (ret) {
		pci_err(pdev, "CXL getting regions params failed\n");
		goto err_region_params;
	}

	pci_info(pdev, "CXL region [%llx - %llx]\n", range.start, range.end);

	return 0;

err_region_params:
        cxl_accel_region_detach(cxl->cxled);
err_region:
        cxl_dpa_free(cxl->cxled);
err_media_ready:
err_memdev:
        cxl_release_resource(cxl->cxlds, CXL_RES_RAM);
err_resource_set:
        kfree(cxl->cxlds);
err_state:
        kfree(cxl);
        return ret;
}

static void qemu_cxl_accel_remove(struct pci_dev *pdev)
{
	struct qemu_cxl_accel *cxl = pci_get_drvdata(pdev);

	cxl_accel_region_detach(cxl->cxled);
	cxl_dpa_free(cxl->cxled);
	cxl_release_resource(cxl->cxlds, CXL_RES_RAM);
	kfree(cxl->cxlds);
	kfree(cxl);
	pci_disable_device(pdev);
}

static struct pci_device_id qemu_cxl_accel_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0d94) },
	{},
};

static struct pci_driver qemu_cxl_accel_pci_driver = {
	.name		= "qemu-cxl-accel",
	.id_table	= qemu_cxl_accel_pci_table,
	.probe		= qemu_cxl_accel_probe,
	.remove		= qemu_cxl_accel_remove,
};

static int __init qemu_cxl_accel_init(void)
{
	int ret;

	ret = pci_register_driver(&qemu_cxl_accel_pci_driver);
	if (ret)
		return ret;

	return 0;
}

module_init(qemu_cxl_accel_init);

MODULE_AUTHOR("Zhi Wang <zhiw@nvidia.com>");
MODULE_DEVICE_TABLE(pci, qemu_cxl_accel_pci_table);
MODULE_DESCRIPTION("QEMU CXL Accel Driver");
MODULE_IMPORT_NS("CXL");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: cxl_core cxl_port cxl_acpi cxl-mem");

