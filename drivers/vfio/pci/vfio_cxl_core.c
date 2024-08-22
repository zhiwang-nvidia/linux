// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
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

/* Standard CXL-type 2 driver initialization sequence */
static int enable_cxl(struct vfio_cxl_core_device *cxl, u16 dvsec,
		      struct vfio_cxl_dev_info *info)
{
	DECLARE_BITMAP(expected, CXL_MAX_CAPS);
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	struct pci_dev *pdev = pci->pdev;
	u64 offset, size, count;
	int ret;

	cxl->cxlds = cxl_accel_state_create(&pdev->dev);
	if (IS_ERR(cxl->cxlds))
		return PTR_ERR(cxl->cxlds);

	cxl_set_dvsec(cxl->cxlds, dvsec);
	cxl_set_serial(cxl->cxlds, pdev->dev.id);

	ret = cxl_set_resource(cxl->cxlds, info->dpa_res, CXL_RES_DPA);
	if (ret)
		goto err;

	ret = cxl_set_resource(cxl->cxlds, info->ram_res, CXL_RES_RAM);
	if (ret)
		goto err;

	ret = cxl_pci_accel_setup_regs(pdev, cxl->cxlds);
	if (ret)
		goto err;

	if (!info->dev_caps)
		bitmap_clear(expected, 0, CXL_MAX_CAPS);
	else
		bitmap_copy(expected, info->dev_caps, CXL_MAX_CAPS);

	bitmap_set(expected, CXL_DEV_CAP_HDM, 1);

	if (!cxl_pci_check_caps(cxl->cxlds, expected, NULL)) {
		ret = -EIO;
		goto err;
	}

	ret = cxl_get_hdm_reg_info(cxl->cxlds, &count, &offset, &size);
	if (ret)
		goto err;

	if (WARN_ON(!count || !size))
		return -ENODEV;

	cxl->hdm_count = count;
	cxl->hdm_reg_offset = offset;
	cxl->hdm_reg_size = size;

	ret = cxl_request_resource(cxl->cxlds, CXL_RES_RAM);
	if (ret)
		goto err;

	/* Some devices don't have media ready support. E.g. AMD SFC. */
	if (!info->no_media_ready) {
		ret = cxl_await_range_active(cxl->cxlds);
		if (ret)
			goto err;
	}

	cxl_set_media_ready(cxl->cxlds);

	cxl->cxlmd = devm_cxl_add_memdev(&pdev->dev, cxl->cxlds);
	if (IS_ERR(cxl->cxlmd)) {
		ret = PTR_ERR(cxl->cxlmd);
		goto err;
	}

	cxl->region.noncached = info->noncached_region;
	return 0;
err:
	kfree(cxl->cxlds);
	return ret;
}

static void disable_cxl(struct vfio_cxl_core_device *cxl)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	struct pci_dev *pdev = pci->pdev;

	WARN_ON(cxl->region.region);

	devm_cxl_del_memdev(&pdev->dev, cxl->cxlmd);
	cxl_release_resource(cxl->cxlds, CXL_RES_RAM);
	kfree(cxl->cxlds);
}

int vfio_cxl_core_enable(struct vfio_cxl_core_device *cxl,
			 struct vfio_cxl_dev_info *info)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	struct pci_dev *pdev = pci->pdev;
	u16 dvsec;
	int ret;

	dvsec = pci_find_dvsec_capability(pdev, PCI_VENDOR_ID_CXL,
					  CXL_DVSEC_PCIE_DEVICE);
	if (!dvsec)
		return -ENODEV;

	ret = vfio_pci_core_enable(pci);
	if (ret)
		return ret;

	ret = enable_cxl(cxl, dvsec, info);
	if (ret)
		goto err;

	return 0;

err:
	vfio_pci_core_disable(pci);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_enable);

void vfio_cxl_core_finish_enable(struct vfio_cxl_core_device *cxl)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;

	vfio_pci_core_finish_enable(pci);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_finish_enable);

static void disable_device(struct vfio_cxl_core_device *cxl)
{
	disable_cxl(cxl);
}

void vfio_cxl_core_disable(struct vfio_cxl_core_device *cxl)
{
	disable_device(cxl);
	vfio_pci_core_disable(&cxl->pci_core);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_disable);

void vfio_cxl_core_close_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl = vfio_pci_core_to_cxl(pci);

	disable_device(cxl);
	vfio_pci_core_close_device(vdev);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_close_device);

static int get_hpa_and_request_dpa(struct vfio_cxl_core_device *cxl, u64 size)
{
	u64 max;

	cxl->cxlrd = cxl_get_hpa_freespace(cxl->cxlmd,
					   CXL_DECODER_F_RAM |
					   CXL_DECODER_F_TYPE2,
					   &max);
	if (IS_ERR(cxl->cxlrd))
		return PTR_ERR(cxl->cxlrd);

	if (max < size)
		return -ENOSPC;

	cxl->cxled = cxl_request_dpa(cxl->cxlmd, true, size, size);
	if (IS_ERR(cxl->cxled))
		return PTR_ERR(cxl->cxled);

	return 0;
}

int vfio_cxl_core_create_cxl_region(struct vfio_cxl_core_device *cxl, u64 size)
{
	struct cxl_region *region;
	struct range range;
	int ret;

	if (WARN_ON(cxl->region.region))
		return -EEXIST;

	ret = get_hpa_and_request_dpa(cxl, size);
	if (ret)
		return ret;

	region = cxl_create_region(cxl->cxlrd, cxl->cxled, true);
	if (IS_ERR(region)) {
		ret = PTR_ERR(region);
		cxl_dpa_free(cxl->cxled);
		return ret;
	}

	cxl_get_region_range(region, &range);

	cxl->region.addr = range.start;
	cxl->region.size = size;
	cxl->region.region = region;
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_create_cxl_region);

void vfio_cxl_core_destroy_cxl_region(struct vfio_cxl_core_device *cxl)
{
	if (!cxl->region.region)
		return;

	cxl_accel_region_detach(cxl->cxled);
	cxl_dpa_free(cxl->cxled);
	cxl->region.region = NULL;
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_destroy_cxl_region);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_IMPORT_NS("CXL");
MODULE_SOFTDEP("pre: cxl_core cxl_port cxl_acpi cxl-mem");
