// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "vfio_pci_priv.h"

#define DRIVER_AUTHOR "Zhi Wang <zhiw@nvidia.com>"
#define DRIVER_DESC "core driver for VFIO based CXL devices"

static int get_hpa_and_request_dpa(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	struct pci_dev *pdev = core_dev->pdev;
	u64 max;

	cxl->cxlrd = cxl_get_hpa_freespace(cxl->endpoint, 1,
					   CXL_DECODER_F_RAM |
					   CXL_DECODER_F_TYPE2,
					   &max);
	if (IS_ERR(cxl->cxlrd)) {
		pci_err(pdev, "Fail to get HPA space.\n");
		return PTR_ERR(cxl->cxlrd);
	}

	if (max < cxl->region.size) {
		pci_err(pdev, "No enough free HPA space %llu < %llu\n",
			max, cxl->region.size);
		return -ENOSPC;
	}

	cxl->cxled = cxl_request_dpa(cxl->endpoint, true, cxl->region.size,
				     cxl->region.size);
	if (IS_ERR(cxl->cxled)) {
		pci_err(pdev, "Fail to request DPA\n");
		return PTR_ERR(cxl->cxled);
	}

	return 0;
}

static int create_cxl_region(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	struct pci_dev *pdev = core_dev->pdev;
	resource_size_t start, end;
	int ret;

	ret = cxl_accel_request_resource(cxl->cxlds, true);
	if (ret) {
		pci_err(pdev, "Fail to request CXL resource\n");
		return ret;
	}

	if (!cxl_await_media_ready(cxl->cxlds)) {
		cxl_accel_set_media_ready(cxl->cxlds);
	} else {
		pci_err(pdev, "CXL media is not active\n");
		return ret;
	}

	cxl->cxlmd = devm_cxl_add_memdev(&pdev->dev, cxl->cxlds);
	if (IS_ERR(cxl->cxlmd)) {
		pci_err(pdev, "Fail to create CXL memdev\n");
		return PTR_ERR(cxl->cxlmd);
	}

	cxl->endpoint = cxl_acquire_endpoint(cxl->cxlmd);
	if (IS_ERR(cxl->endpoint)) {
		pci_err(pdev, "Fail to acquire CXL endpoint\n");
		return PTR_ERR(cxl->endpoint);
	}

	ret = get_hpa_and_request_dpa(core_dev);
	if (ret)
		goto out;

	cxl->region.region = cxl_create_region(cxl->cxlrd, &cxl->cxled, 1);
	if (IS_ERR(cxl->region.region)) {
		ret = PTR_ERR(cxl->region.region);
		pci_err(pdev, "Fail to create CXL region\n");
		cxl_dpa_free(cxl->cxled);
		goto out;
	}

	cxl_accel_get_region_params(cxl->region.region, &start, &end);

	cxl->region.addr = start;
out:
	cxl_release_endpoint(cxl->cxlmd, cxl->endpoint);
	return ret;
}

/* Standard CXL-type 2 driver initialization sequence */
static int enable_cxl(struct vfio_pci_core_device *core_dev, u16 dvsec)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	struct pci_dev *pdev = core_dev->pdev;
	u32 count;
	u64 offset, size;
	int ret;

	cxl->cxlds = cxl_accel_state_create(&pdev->dev, cxl->caps);
	if (IS_ERR(cxl->cxlds))
		return PTR_ERR(cxl->cxlds);

	cxl_accel_set_dvsec(cxl->cxlds, dvsec);
	cxl_accel_set_serial(cxl->cxlds, pdev->dev.id);

	cxl_accel_set_resource(cxl->cxlds, cxl->dpa_res, CXL_ACCEL_RES_DPA);
	cxl_accel_set_resource(cxl->cxlds, cxl->ram_res, CXL_ACCEL_RES_RAM);

	ret = cxl_pci_accel_setup_regs(pdev, cxl->cxlds);
	if (ret) {
		pci_err(pdev, "Fail to setup CXL accel regs\n");
		return ret;
	}

	ret = cxl_get_hdm_info(cxl->cxlds, &count, &offset, &size);
	if (ret)
		return ret;

	if (!count || !size) {
		pci_err(pdev, "Fail to find CXL HDM reg offset\n");
		return -ENODEV;
	}

	cxl->hdm_count = count;
	cxl->hdm_reg_offset = offset;
	cxl->hdm_reg_size = size;

	return create_cxl_region(core_dev);
}

static void disable_cxl(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;

	if (cxl->region.region)
		cxl_region_detach(cxl->cxled);

	if (cxl->cxled)
		cxl_dpa_free(cxl->cxled);
}

int vfio_cxl_core_enable(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	struct pci_dev *pdev = core_dev->pdev;
	u16 dvsec;
	int ret;

	dvsec = pci_find_dvsec_capability(pdev, PCI_VENDOR_ID_CXL,
					  CXL_DVSEC_PCIE_DEVICE);
	if (!dvsec)
		return -ENODEV;

	if (!cxl->region.size)
		return -EINVAL;

	ret = vfio_pci_core_enable(core_dev);
	if (ret)
		return ret;

	ret = enable_cxl(core_dev, dvsec);
	if (ret)
		goto err_enable_cxl_device;

	return 0;

err_enable_cxl_device:
	vfio_pci_core_disable(core_dev);
	return ret;
}
EXPORT_SYMBOL(vfio_cxl_core_enable);

void vfio_cxl_core_finish_enable(struct vfio_pci_core_device *core_dev)
{
	vfio_pci_core_finish_enable(core_dev);
}
EXPORT_SYMBOL(vfio_cxl_core_finish_enable);

void vfio_cxl_core_close_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev =
		container_of(vdev, struct vfio_pci_core_device, vdev);

	disable_cxl(core_dev);
	vfio_pci_core_close_device(vdev);
}
EXPORT_SYMBOL(vfio_cxl_core_close_device);

/*
 * Configure the resource required by the kernel CXL core:
 * device DPA and device RAM size
 */
void vfio_cxl_core_set_resource(struct vfio_pci_core_device *core_dev,
				struct resource res,
				enum accel_resource type)
{
	struct vfio_cxl *cxl = &core_dev->cxl;

	switch (type) {
	case CXL_ACCEL_RES_DPA:
		cxl->dpa_size = res.end - res.start + 1;
		cxl->dpa_res = res;
		break;

	case CXL_ACCEL_RES_RAM:
		cxl->ram_res = res;
		break;

	default:
		WARN(1, "invalid resource type: %d\n", type);
		break;
	}
}
EXPORT_SYMBOL(vfio_cxl_core_set_resource);

/* Configure the expected CXL region size to be created */
void vfio_cxl_core_set_region_size(struct vfio_pci_core_device *core_dev,
				   u64 size)
{
	struct vfio_cxl *cxl = &core_dev->cxl;

	if (WARN_ON(size > cxl->dpa_size))
		return;

	if (WARN_ON(cxl->region.region))
		return;

	cxl->region.size = size;
}
EXPORT_SYMBOL(vfio_cxl_core_set_region_size);

/* Configure the driver cap required by the kernel CXL core */
void vfio_cxl_core_set_driver_hdm_cap(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;

	cxl->caps |= CXL_ACCEL_DRIVER_CAP_HDM;
}
EXPORT_SYMBOL(vfio_cxl_core_set_driver_hdm_cap);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_IMPORT_NS(CXL);
