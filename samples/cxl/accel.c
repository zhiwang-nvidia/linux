// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>
#include <linux/cxl_accel_mem.h>
#include <linux/cxl_accel_pci.h>

#define BUFFER_SIZE	(SZ_256M)

struct cxl_accel {
	cxl_accel_state *cxlds;
	struct cxl_memdev *cxlmd;
	struct cxl_root_decoder *cxlrd;
	struct cxl_port *endpoint;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_region *region;
};

static int cxl_accel_probe(struct pci_dev *pdev,
			 const struct pci_device_id *ent)
{
	resource_size_t start, end, max = 0;
	struct cxl_accel *cxl;
	struct resource res;
	u16 dvsec;
	int ret = 0;

	dvsec = pci_find_dvsec_capability(pdev, PCI_VENDOR_ID_CXL,
					  CXL_DVSEC_PCIE_DEVICE);

	if (!dvsec)
		return -ENODEV;

	pci_info(pdev, "CXL CXL_DVSEC_PCIE_DEVICE capability found");

	cxl = devm_kzalloc(&pdev->dev, sizeof(*cxl), GFP_KERNEL);
	if (!cxl)
		return -ENOMEM;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;

	cxl->cxlds = cxl_accel_state_create(&pdev->dev,
					    CXL_ACCEL_DRIVER_CAP_HDM);
	if (IS_ERR(cxl->cxlds)) {
		pci_err(pdev, "CXL accel device state failed");
		return PTR_ERR(cxl->cxlds);
	}

	cxl_accel_set_dvsec(cxl->cxlds, dvsec);
	cxl_accel_set_serial(cxl->cxlds, pdev->dev.id);

	res = DEFINE_RES_MEM(0, BUFFER_SIZE);
	cxl_accel_set_resource(cxl->cxlds, res, CXL_ACCEL_RES_DPA);

	res = DEFINE_RES_MEM_NAMED(0, BUFFER_SIZE, "ram");
	cxl_accel_set_resource(cxl->cxlds, res, CXL_ACCEL_RES_RAM);

	if (cxl_pci_accel_setup_regs(pdev, cxl->cxlds)) {
		pci_err(pdev, "CXL accel setup regs failed");
		return -ENODEV;
	}

	if (cxl_accel_request_resource(cxl->cxlds, true))
		pci_err(pdev, "CXL accel resource request failed");

	if (!cxl_await_media_ready(cxl->cxlds)) {
		cxl_accel_set_media_ready(cxl->cxlds);
	} else {
		pci_err(pdev, "CXL accel media not active");
		return -ENODEV;
	}

	cxl->cxlmd = devm_cxl_add_memdev(&pdev->dev, cxl->cxlds);
	if (IS_ERR(cxl->cxlmd)) {
		pci_err(pdev, "CXL accel memdev creation failed");
		return PTR_ERR(cxl->cxlmd);
	}

	cxl->endpoint = cxl_acquire_endpoint(cxl->cxlmd);
	if (IS_ERR(cxl->endpoint))
		pci_err(pdev, "CXL accel acquire endpoint failed");

	cxl->cxlrd = cxl_get_hpa_freespace(cxl->endpoint, 1,
					    CXL_DECODER_F_RAM | CXL_DECODER_F_TYPE2,
					    &max);

	if (IS_ERR(cxl->cxlrd)) {
		pci_err(pdev, "CXL accel get HPA failed");
		ret = PTR_ERR(cxl->cxlrd);
		goto out;
	}

	if (max < BUFFER_SIZE) {
		pci_err(pdev, "CXL accel not enough free HPA space %llu < %u\n",
				  max,BUFFER_SIZE);
		ret = -ENOMEM;
		goto out;
	}

	cxl->cxled = cxl_request_dpa(cxl->endpoint, true, BUFFER_SIZE,
				     BUFFER_SIZE);
	if (IS_ERR(cxl->cxled)) {
		pci_err(pdev, "CXL accel request DPA failed");
		return PTR_ERR(cxl->cxled);
	}

	cxl->region = cxl_create_region(cxl->cxlrd, &cxl->cxled, 1);
	if (!cxl->region) {
		pci_err(pdev, "CXL accel create region failed");
		cxl_dpa_free(cxl->cxled);
		return -ENOMEM;
	}

	cxl_accel_get_region_params(cxl->region, &start, &end);

	pci_info(pdev, "QEMU ACCEL device driver is loaded");
out:
	cxl_release_endpoint(cxl->cxlmd, cxl->endpoint);
	pci_set_drvdata(pdev, cxl);
	return ret;
}

static void cxl_accel_remove(struct pci_dev *pdev)
{
	struct cxl_accel *cxl = pci_get_drvdata(pdev);

	if (cxl->region)
		cxl_region_detach(cxl->cxled);

	if (cxl->cxled)
		cxl_dpa_free(cxl->cxled);

	pci_disable_device(pdev);
 }

static struct pci_device_id cxl_accel_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0d94) },
	{},
};

static struct pci_driver cxl_accel_pci_driver = {
	.name		= "cxl-accel",
	.id_table	= cxl_accel_pci_table,
	.probe		= cxl_accel_probe,
	.remove		= cxl_accel_remove,
};

static int __init cxl_accel_init(void)
{
	int ret;

	ret = pci_register_driver(&cxl_accel_pci_driver);
	if (ret)
		return ret;

	return 0;
}

module_init(cxl_accel_init);

MODULE_DEVICE_TABLE(pci, cxl_accel_pci_table);
MODULE_DESCRIPTION("QEMU CXL Accel driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
