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
	cxl->region.vaddr = ioremap(start, end - start);
	if (!cxl->region.addr) {
		pci_err(pdev, "Fail to map CXL region\n");
		cxl_region_detach(cxl->cxled);
		cxl_dpa_free(cxl->cxled);
		goto out;
	}
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

	if (cxl->region.region) {
		iounmap(cxl->region.vaddr);
		cxl_region_detach(cxl->cxled);
	}

	if (cxl->cxled)
		cxl_dpa_free(cxl->cxled);
}

static unsigned long vma_to_pfn(struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *vdev = vma->vm_private_data;
	struct vfio_cxl *cxl = &vdev->cxl;
	u64 pgoff;

	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);

	return (cxl->region.addr >> PAGE_SHIFT) + pgoff;
}

static vm_fault_t vfio_cxl_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct vfio_pci_core_device *vdev = vma->vm_private_data;
	unsigned long pfn, pgoff = vmf->pgoff - vma->vm_pgoff;
	unsigned long addr = vma->vm_start;
	vm_fault_t ret = VM_FAULT_SIGBUS;

	pfn = vma_to_pfn(vma);

	down_read(&vdev->memory_lock);

	if (vdev->pm_runtime_engaged || !__vfio_pci_memory_enabled(vdev))
		goto out_unlock;

	ret = vmf_insert_pfn(vma, vmf->address, pfn + pgoff);
	if (ret & VM_FAULT_ERROR)
		goto out_unlock;

	for (; addr < vma->vm_end; addr += PAGE_SIZE, pfn++) {
		if (addr == vmf->address)
			continue;

		if (vmf_insert_pfn(vma, addr, pfn) & VM_FAULT_ERROR)
			break;
	}

out_unlock:
	up_read(&vdev->memory_lock);

	return ret;
}

static const struct vm_operations_struct vfio_cxl_mmap_ops = {
	.fault = vfio_cxl_mmap_fault,
};

static int vfio_cxl_region_mmap(struct vfio_pci_core_device *core_dev,
				struct vfio_pci_region *region,
				struct vm_area_struct *vma)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	u64 phys_len, req_len, pgoff, req_start;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_READ) &&
	    (vma->vm_flags & VM_READ))
		return -EPERM;

	if (!(region->flags & VFIO_REGION_INFO_FLAG_WRITE) &&
	    (vma->vm_flags & VM_WRITE))
		return -EPERM;

	phys_len = cxl->region.size;
	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (req_start + req_len > phys_len)
		return -EINVAL;

	vma->vm_private_data = core_dev;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	vm_flags_set(vma, VM_ALLOW_ANY_UNCACHED | VM_IO | VM_PFNMAP |
			VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &vfio_cxl_mmap_ops;

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

int vfio_cxl_core_enable(struct vfio_pci_core_device *core_dev)
{
	struct vfio_cxl *cxl = &core_dev->cxl;
	struct pci_dev *pdev = core_dev->pdev;
	u32 flags;
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

	flags = VFIO_REGION_INFO_FLAG_READ |
		VFIO_REGION_INFO_FLAG_WRITE |
		VFIO_REGION_INFO_FLAG_MMAP;

	ret = vfio_pci_core_register_dev_region(core_dev,
		PCI_VENDOR_ID_CXL | VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
		VFIO_REGION_SUBTYPE_CXL, &vfio_cxl_regops,
		cxl->region.size, flags, &cxl->region);
	if (ret)
		goto err_register_cxl_region;

	return 0;

err_register_cxl_region:
	disable_cxl(core_dev);
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
MODULE_IMPORT_NS(CXL);
