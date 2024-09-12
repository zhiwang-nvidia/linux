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

	if (cxl->region.precommitted) {
		kfree(cxl->region.region);
		cxl->region.region = NULL;
	}

	WARN_ON(cxl->region.region);

	devm_cxl_del_memdev(&pdev->dev, cxl->cxlmd);
	cxl_release_resource(cxl->cxlds, CXL_RES_RAM);
	kfree(cxl->cxlds);
}

static void discover_precommitted_region(struct vfio_cxl_core_device *cxl)
{
	struct cxl_region **cxlrs = NULL;
	int num, ret;

	ret = cxl_get_committed_regions(cxl->cxlmd, &cxlrs, &num);
	if (ret || !cxlrs) {
		kfree(cxlrs);
		return;
	}

	WARN_ON(num > 1);
	cxl->region.region = cxlrs[0];
	cxl->region.precommitted = true;
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

	discover_precommitted_region(cxl);

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

static int vfio_cxl_region_mmap(struct vfio_pci_core_device *pci,
				struct vfio_pci_region *region,
				struct vm_area_struct *vma)
{
	struct vfio_cxl_region *cxl_region = region->data;
	u64 phys_len, req_len, pgoff, req_start;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_READ) &&
	    (vma->vm_flags & VM_READ))
		return -EPERM;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_WRITE) &&
	    (vma->vm_flags & VM_WRITE))
		return -EPERM;

	phys_len = cxl_region->size;
	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (req_start + req_len > phys_len)
		return -EINVAL;

	vma->vm_private_data = pci;
	if (cxl_region->noncached)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	vm_flags_set(vma, VM_ALLOW_ANY_UNCACHED | VM_IO | VM_PFNMAP |
			VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &vfio_pci_mmap_ops;

	return 0;
}

static ssize_t vfio_cxl_region_rw(struct vfio_pci_core_device *core_dev,
				  char __user *buf, size_t count, loff_t *ppos,
				  bool iswrite)
{
	unsigned int i = VFIO_PCI_OFFSET_TO_INDEX(*ppos) - VFIO_PCI_NUM_REGIONS;
	struct vfio_cxl_region *cxl_region = core_dev->region[i].data;
	loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;

	if (!count)
		return 0;

	return vfio_pci_core_do_io_rw(core_dev, false,
				      cxl_region->vaddr,
				      (char __user *)buf, pos, count,
				      0, 0, iswrite);
}

static void vfio_cxl_region_release(struct vfio_pci_core_device *vdev,
				    struct vfio_pci_region *region)
{
}

static const struct vfio_pci_regops vfio_cxl_regops = {
	.rw		= vfio_cxl_region_rw,
	.mmap		= vfio_cxl_region_mmap,
	.release	= vfio_cxl_region_release,
};

int vfio_cxl_core_register_cxl_region(struct vfio_cxl_core_device *cxl)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	u32 flags;
	int ret;

	if (WARN_ON(!cxl->region.region || cxl->region.vaddr))
		return -EEXIST;

	cxl->region.vaddr = ioremap(cxl->region.addr, cxl->region.size);
	if (!cxl->region.addr)
		return -EFAULT;

	flags = VFIO_REGION_INFO_FLAG_READ |
		VFIO_REGION_INFO_FLAG_WRITE |
		VFIO_REGION_INFO_FLAG_MMAP;

	ret = vfio_pci_core_register_dev_region(pci,
		PCI_VENDOR_ID_CXL | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
		VFIO_REGION_SUBTYPE_CXL, &vfio_cxl_regops,
		cxl->region.size, flags, &cxl->region);
	if (ret) {
		iounmap(cxl->region.vaddr);
		cxl->region.vaddr = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_register_cxl_region);

void vfio_cxl_core_unregister_cxl_region(struct vfio_cxl_core_device *cxl)
{
	if (WARN_ON(!cxl->region.region || !cxl->region.vaddr))
		return;

	iounmap(cxl->region.vaddr);
	cxl->region.vaddr = NULL;
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_unregister_cxl_region);

ssize_t vfio_cxl_core_read(struct vfio_device *core_vdev, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	return vfio_pci_rw(vdev, buf, count, ppos, false);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_read);

ssize_t vfio_cxl_core_write(struct vfio_device *core_vdev, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	return vfio_pci_rw(vdev, (char __user *)buf, count, ppos, true);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_write);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_IMPORT_NS("CXL");
MODULE_SOFTDEP("pre: cxl_core cxl_port cxl_acpi cxl-mem");
