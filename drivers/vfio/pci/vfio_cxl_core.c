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
#include "vfio_cxl_core_priv.h"

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

static int find_bar(struct pci_dev *pdev, u64 *offset, int *bar, u64 size)
{
	u64 start, end, flags;
	int index, i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		index = i + PCI_STD_RESOURCES;
		flags = pci_resource_flags(pdev, index);

		start = pci_resource_start(pdev, index);
		end = pci_resource_end(pdev, index);

		if (*offset >= start && *offset + size - 1 <= end)
			break;

		if (flags & IORESOURCE_MEM_64)
			i++;
	}

	if (i == PCI_STD_NUM_BARS)
		return -ENODEV;

	*offset = *offset - start;
	*bar = index;

	return 0;
}

static int find_comp_regs(struct vfio_cxl_core_device *cxl)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	struct pci_dev *pdev = pci->pdev;
	u64 offset;
	int ret, bar;

	ret = cxl_find_comp_regblock_offset(pdev, &offset);
	if (ret)
		return ret;

	ret = find_bar(pdev, &offset, &bar, SZ_64K);
	if (ret)
		return ret;

	cxl->comp_reg_bar = bar;
	cxl->comp_reg_offset = offset;
	cxl->comp_reg_size = SZ_64K;
	return 0;
}

static void clean_virt_regs(struct vfio_cxl_core_device *cxl)
{
	kvfree(cxl->comp_reg_virt);
	kvfree(cxl->config_virt);
}

static void reset_virt_regs(struct vfio_cxl_core_device *cxl)
{
	memcpy(cxl->config_virt, cxl->initial_config_virt, cxl->config_size);
	memcpy(cxl->comp_reg_virt, cxl->initial_comp_reg_virt, cxl->comp_reg_size);
}

static int setup_virt_regs(struct vfio_cxl_core_device *cxl)
{
	struct vfio_pci_core_device *pci = &cxl->pci_core;
	struct pci_dev *pdev = pci->pdev;
	u64 offset = cxl->comp_reg_offset;
	int bar = cxl->comp_reg_bar;
	u64 size = cxl->comp_reg_size;
	void *regs;
	unsigned int i;

	regs = kvzalloc(size * 2, GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	cxl->comp_reg_virt = regs;
	cxl->initial_comp_reg_virt = regs + size;

	regs = ioremap(pci_resource_start(pdev, bar) + offset, size);
	if (!regs) {
		kvfree(cxl->comp_reg_virt);
		return -EFAULT;
	}

	for (i = 0; i < size; i += 4)
		*(u32 *)(cxl->initial_comp_reg_virt + i) =
			cpu_to_le32(readl(regs + i));

	iounmap(regs);

	regs = kvzalloc(pdev->cfg_size * 2, GFP_KERNEL);
	if (!regs) {
		kvfree(cxl->comp_reg_virt);
		return -ENOMEM;
	}

	cxl->config_virt = regs;
	cxl->initial_config_virt = regs + pdev->cfg_size;
	cxl->config_size = pdev->cfg_size;

	regs = cxl->initial_config_virt + cxl->dvsec;

	for (i = 0; i < 0x40; i += 4) {
		u32 val;

		pci_read_config_dword(pdev, cxl->dvsec + i, &val);
		*(u32 *)(regs + i) = cpu_to_le32(val);
	}

	reset_virt_regs(cxl);
	return 0;
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

	cxl->dvsec = dvsec;

	ret = find_comp_regs(cxl);
	if (ret)
		return ret;

	ret = setup_virt_regs(cxl);
	if (ret)
		return ret;

	ret = vfio_pci_core_enable(pci);
	if (ret)
		goto err_pci_core_enable;

	ret = enable_cxl(cxl, dvsec, info);
	if (ret)
		goto err_enable_cxl;

	ret = vfio_cxl_core_setup_register_emulation(cxl);
	if (ret)
		goto err_register_emulation;

	discover_precommitted_region(cxl);

	return 0;

err_register_emulation:
	disable_cxl(cxl);
err_pci_core_enable:
	clean_virt_regs(cxl);
err_enable_cxl:
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
	vfio_cxl_core_clean_register_emulation(cxl);
	disable_cxl(cxl);
	clean_virt_regs(cxl);
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
	struct vfio_cxl_core_device *cxl =
		container_of(vdev, struct vfio_cxl_core_device, pci_core);
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);

	if (!count)
		return 0;

	if (index == VFIO_PCI_CONFIG_REGION_INDEX)
		return vfio_cxl_core_config_rw(core_vdev, buf, count, ppos,
					       false);

	if (index == cxl->comp_reg_bar)
		return vfio_cxl_core_mmio_bar_rw(core_vdev, buf, count, ppos,
						 false);

	return vfio_pci_rw(vdev, buf, count, ppos, false);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_read);

ssize_t vfio_cxl_core_write(struct vfio_device *core_vdev, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(vdev, struct vfio_cxl_core_device, pci_core);
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);

	if (!count)
		return 0;

	if (index == VFIO_PCI_CONFIG_REGION_INDEX)
		return vfio_cxl_core_config_rw(core_vdev, (char __user *)buf,
					       count, ppos, true);

	if (index == cxl->comp_reg_bar)
		return vfio_cxl_core_mmio_bar_rw(core_vdev, (char __user *)buf,
						 count, ppos, true);

	return vfio_pci_rw(vdev, (char __user *)buf, count, ppos, true);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_write);

static int comp_reg_bar_get_region_info(struct vfio_pci_core_device *pci,
					void __user *uarg)
{
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	struct pci_dev *pdev = pci->pdev;
	unsigned long minsz = offsetofend(struct vfio_region_info, offset);
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	struct vfio_region_info_cap_sparse_mmap *sparse;
	struct vfio_region_info info;
	u64 start, end, len;
	u32 size;
	int ret;

	if (copy_from_user(&info, uarg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	start = pci_resource_start(pdev, cxl->comp_reg_bar);
	end = pci_resource_end(pdev, cxl->comp_reg_bar);
	len = pci_resource_len(pdev, cxl->comp_reg_bar);

	if (cxl->comp_reg_offset == start ||
	    cxl->comp_reg_offset + cxl->comp_reg_size == end) {
		size = struct_size(sparse, areas, 1);

		sparse = kzalloc(size, GFP_KERNEL);
		if (!sparse)
			return -ENOMEM;

		sparse->areas[0].offset = cxl->comp_reg_offset - start;
		sparse->areas[0].size = cxl->comp_reg_size;
	} else {
		size = struct_size(sparse, areas, 2);

		sparse = kzalloc(size, GFP_KERNEL);
		if (!sparse)
			return -ENOMEM;

		sparse->areas[0].offset = 0;
		sparse->areas[0].size = cxl->comp_reg_offset - start;

		sparse->areas[1].offset = sparse->areas[0].size + cxl->comp_reg_size;
		sparse->areas[1].size = len - sparse->areas[0].size -
					cxl->comp_reg_size;
	}

	sparse->header.id = VFIO_REGION_INFO_CAP_SPARSE_MMAP;
	sparse->header.version = 1;

	ret = vfio_info_add_capability(&caps, &sparse->header, size);
	kfree(sparse);
	if (ret)
		return ret;

	info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
	info.size = len;
	info.flags = VFIO_REGION_INFO_FLAG_READ |
		     VFIO_REGION_INFO_FLAG_WRITE |
		     VFIO_REGION_INFO_FLAG_MMAP;

	if (caps.size) {
		info.flags |= VFIO_REGION_INFO_FLAG_CAPS;
		if (info.argsz < sizeof(info) + caps.size) {
			info.argsz = sizeof(info) + caps.size;
			info.cap_offset = 0;
		} else {
			vfio_info_cap_shift(&caps, sizeof(info));
			if (copy_to_user(uarg + sizeof(info), caps.buf,
					 caps.size)) {
				kfree(caps.buf);
				return -EFAULT;
			}
			info.cap_offset = sizeof(info);
		}
		kfree(caps.buf);
	}
	return copy_to_user(uarg, &info, minsz) ? -EFAULT : 0;
}

long vfio_cxl_core_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
			 unsigned long arg)
{
	struct vfio_pci_core_device *pci =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	void __user *uarg = (void __user *)arg;

	if (cmd == VFIO_DEVICE_GET_REGION_INFO) {
		struct vfio_region_info info;
		unsigned long minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index == cxl->comp_reg_bar)
			return comp_reg_bar_get_region_info(pci, uarg);
	}
	return vfio_pci_core_ioctl(core_vdev, cmd, arg);
}
EXPORT_SYMBOL_GPL(vfio_cxl_core_ioctl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_IMPORT_NS("CXL");
MODULE_SOFTDEP("pre: cxl_core cxl_port cxl_acpi cxl-mem");
