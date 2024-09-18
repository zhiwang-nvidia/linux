/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vfio_pci_core.h>
#include <linux/types.h>

#include "vfio.h"

static int pdev_to_gfid(struct pci_dev *pdev)
{
	return pci_iov_vf_id(pdev) + 1;
}

static int destroy_vgpu(struct nvidia_vgpu_vfio *nvdev)
{
	int ret;

	ret = nvidia_vgpu_mgr_destroy_vgpu(nvdev->vgpu);
	if (ret)
		return ret;

	kfree(nvdev->vgpu);
	nvdev->vgpu = NULL;
	return 0;
}

static int create_vgpu(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	struct nvidia_vgpu *vgpu;
	int ret;

	vgpu = kzalloc(sizeof(*vgpu), GFP_KERNEL);
	if (!vgpu)
		return -ENOMEM;

	vgpu->info.id = pci_iov_vf_id(pdev);
	vgpu->info.dbdf = (0 << 16) | pci_dev_id(pdev);
	vgpu->info.gfid = pdev_to_gfid(pdev);

	vgpu->vgpu_mgr = vgpu_mgr;
	vgpu->pdev = pdev;

	ret = nvidia_vgpu_mgr_create_vgpu(vgpu,
			(u8 *)nvdev->curr_vgpu_type);
	if (ret) {
		kfree(vgpu);
		return ret;
	}

	pr_err("create_vgpu() called\n");
	nvdev->vgpu = vgpu;
	return 0;
}

static inline struct vfio_pci_core_device *
vdev_to_core_dev(struct vfio_device *vdev)
{
	return container_of(vdev, struct vfio_pci_core_device, vdev);
}

static inline struct nvidia_vgpu_vfio *
core_dev_to_nvdev(struct vfio_pci_core_device *core_dev)
{
	return container_of(core_dev, struct nvidia_vgpu_vfio, core_dev);
}

static void detach_vgpu_mgr(struct nvidia_vgpu_vfio *nvdev)
{
	nvidia_vgpu_mgr_put(nvdev->vgpu_mgr);

	nvdev->vgpu_mgr = NULL;
	nvdev->vgpu_types = NULL;
	nvdev->num_vgpu_types = 0;
}

static int attach_vgpu_mgr(struct nvidia_vgpu_vfio *nvdev,
			   struct pci_dev *pdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr;

	vgpu_mgr = nvidia_vgpu_mgr_get(pdev);
	if (IS_ERR(vgpu_mgr))
		return PTR_ERR(vgpu_mgr);

	nvdev->vgpu_mgr = vgpu_mgr;
	nvdev->vgpu_types = nvdev->vgpu_mgr->vgpu_types;
	nvdev->num_vgpu_types = nvdev->vgpu_mgr->num_vgpu_types;

	return 0;
}

static NVA081_CTRL_VGPU_INFO *
find_vgpu_type(struct nvidia_vgpu_vfio *nvdev, u32 type_id)
{
	NVA081_CTRL_VGPU_INFO *vgpu_type;
	u32 i;

	for (i = 0; i < nvdev->num_vgpu_types; i++) {
		vgpu_type = (NVA081_CTRL_VGPU_INFO *)nvdev->vgpu_types[i];
		if (vgpu_type->vgpuType == type_id)
			return vgpu_type;
	}

	return NULL;
}

static int
nvidia_vgpu_vfio_open_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;
	u64 pf_dma_mask;
	int ret;

	if (!nvdev->curr_vgpu_type)
		return -ENODEV;

	if (!pdev->physfn)
		return -EINVAL;

	ret = create_vgpu(nvdev);
	if (ret)
		return ret;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_enable_device;

	pci_set_master(pdev);

	pf_dma_mask = dma_get_mask(&pdev->physfn->dev);
	dma_set_mask(&pdev->dev, pf_dma_mask);
	dma_set_coherent_mask(&pdev->dev, pf_dma_mask);

	ret = pci_try_reset_function(pdev);
	if (ret)
		goto err_reset_function;

	ret = nvidia_vgpu_mgr_enable_bme(nvdev->vgpu);
	if (ret)
		goto err_enable_bme;

	return 0;

err_enable_bme:
err_reset_function:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
err_enable_device:
	destroy_vgpu(nvdev);
	return ret;
}

static void
nvidia_vgpu_vfio_close_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;

	WARN_ON(destroy_vgpu(nvdev));

	if (nvdev->bar0_map) {
		iounmap(nvdev->bar0_map);
		pci_release_selected_regions(pdev, 1 << 0);
		nvdev->bar0_map = NULL;
	}

	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

static int
get_region_info(struct vfio_pci_core_device *core_dev, unsigned long arg)
{
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;
	struct vfio_region_info info;
	unsigned long minsz;
	int ret = 0;

	minsz = offsetofend(struct vfio_region_info, offset);
	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EINVAL;

	if (info.argsz < minsz)
		return -EINVAL;

	switch (info.index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = PCI_CONFIG_SPACE_LENGTH;
		info.flags = VFIO_REGION_INFO_FLAG_READ |
			VFIO_REGION_INFO_FLAG_WRITE;
		break;

	case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR4_REGION_INDEX:
		struct vfio_info_cap caps = { .buf = NULL, .size = 0 };

		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = pci_resource_len(pdev, info.index);

		if (info.index == VFIO_PCI_BAR1_REGION_INDEX)
			info.size = nvdev->curr_vgpu_type->bar1Length * 1024 * 1024;

		if (!info.size) {
			info.flags = 0;
			break;
		}
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
				if (copy_to_user((void __user *)arg +
							sizeof(info), caps.buf,
							caps.size)) {
					kfree(caps.buf);
					ret = -EFAULT;
					break;
				}
				info.cap_offset = sizeof(info);
			}
			kfree(caps.buf);
		}
		break;
	case VFIO_PCI_BAR5_REGION_INDEX:
	case VFIO_PCI_ROM_REGION_INDEX:
	case VFIO_PCI_VGA_REGION_INDEX:
		info.size = 0;
		break;

	default:
		if (info.index >= VFIO_PCI_NUM_REGIONS)
			ret = -EINVAL;
		break;
	}

	if (!ret)
		ret = copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;

	return ret;
}

static long nvidia_vgpu_vfio_ioctl(struct vfio_device *vdev,
				   unsigned int cmd,
				   unsigned long arg)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	int ret = 0;

	if (!nvdev->curr_vgpu_type)
		return -ENODEV;

	switch (cmd) {
	case VFIO_DEVICE_GET_REGION_INFO:
		ret = get_region_info(core_dev, arg);
		break;
	case VFIO_DEVICE_GET_PCI_HOT_RESET_INFO:
	case VFIO_DEVICE_PCI_HOT_RESET:
	case VFIO_DEVICE_RESET:
		break;

	default:
		ret = vfio_pci_core_ioctl(vdev, cmd, arg);
		break;
	}

	return ret;
}

static ssize_t nvidia_vgpu_vfio_read(struct vfio_device *vdev,
				     char __user *buf, size_t count,
				     loff_t *ppos)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	u64 val;
	size_t done = 0;
	int ret = 0, size;

	if (!nvdev->curr_vgpu_type)
		return -ENODEV;

	while (count) {
		if (count >= 4 && !(*ppos % 4))
			size = 4;
		else if (count >= 2 && !(*ppos % 2))
			size = 2;
		else
			size = 1;

		ret = nvidia_vgpu_vfio_access(nvdev, (char *)&val, size, *ppos, false);

		if (ret <= 0)
			return ret;

		if (copy_to_user(buf, &val, size) != 0)
			return -EFAULT;

		*ppos += size;
		buf += size;
		count -= size;
		done += size;
	}

	return done;
}

static ssize_t nvidia_vgpu_vfio_write(struct vfio_device *vdev,
				      const char __user *buf, size_t count,
				      loff_t *ppos)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	u64 val;
	size_t done = 0;
	int ret = 0, size;

	if (!nvdev->curr_vgpu_type)
		return -ENODEV;

	while (count) {
		if (count >= 4 && !(*ppos % 4))
			size = 4;
		else if (count >= 2 && !(*ppos % 2))
			size = 2;
		else
			size = 1;

		if (copy_from_user(&val, buf, size) != 0)
			return -EFAULT;

		ret = nvidia_vgpu_vfio_access(nvdev, (char *)&val, size, *ppos, true);

		if (ret <= 0)
			return ret;

		*ppos += size;
		buf += size;
		count -= size;
		done += size;
	}

	return done;
}

static int nvidia_vgpu_vfio_mmap(struct vfio_device *vdev,
				 struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;
	u64 phys_len, req_len, pgoff, req_start;
	unsigned int index;

	if (!nvdev->curr_vgpu_type)
		return -ENODEV;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);

	if (index >= VFIO_PCI_BAR5_REGION_INDEX)
		return -EINVAL;
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	phys_len = PAGE_ALIGN(pci_resource_len(pdev, index));
	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (req_len == 0)
		return -EINVAL;

	if ((req_start + req_len > phys_len) || (phys_len == 0))
		return -EINVAL;

	vma->vm_private_data = vdev;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = (pci_resource_start(pdev, index) >> PAGE_SHIFT) + pgoff;
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, req_len, vma->vm_page_prot);
}

static const struct vfio_device_ops nvidia_vgpu_vfio_ops = {
	.name           = "nvidia-vgpu-vfio-pci",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device    = nvidia_vgpu_vfio_open_device,
	.close_device   = nvidia_vgpu_vfio_close_device,
	.ioctl          = nvidia_vgpu_vfio_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read           = nvidia_vgpu_vfio_read,
	.write          = nvidia_vgpu_vfio_write,
	.mmap           = nvidia_vgpu_vfio_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static int setup_vgpu_type(struct nvidia_vgpu_vfio *nvdev)
{
	nvdev->curr_vgpu_type = find_vgpu_type(nvdev, 869);
	if (!nvdev->curr_vgpu_type)
		return -ENODEV;
	return 0;
}

static int nvidia_vgpu_vfio_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id_table)
{
	struct nvidia_vgpu_vfio *nvdev;
	int ret;

	if (!pdev->is_virtfn)
		return -EINVAL;

	nvdev = vfio_alloc_device(nvidia_vgpu_vfio, core_dev.vdev,
				  &pdev->dev, &nvidia_vgpu_vfio_ops);
	if (IS_ERR(nvdev))
		return PTR_ERR(nvdev);

	ret = attach_vgpu_mgr(nvdev, pdev);
	if (ret)
		goto err_attach_vgpu_mgr;

	ret = setup_vgpu_type(nvdev);
	if (ret)
		goto err_setup_vgpu_type;

	nvidia_vgpu_vfio_setup_config(nvdev);

	dev_set_drvdata(&pdev->dev, &nvdev->core_dev);

	ret = vfio_pci_core_register_device(&nvdev->core_dev);
	if (ret)
		goto err_setup_vgpu_type;

	return 0;

err_setup_vgpu_type:
	detach_vgpu_mgr(nvdev);

err_attach_vgpu_mgr:
	vfio_put_device(&nvdev->core_dev.vdev);

	pci_err(pdev, "VF probe failed with ret: %d\n", ret);
	return ret;
}

static void nvidia_vgpu_vfio_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_dev = dev_get_drvdata(&pdev->dev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);

	vfio_pci_core_unregister_device(core_dev);
	detach_vgpu_mgr(nvdev);
	vfio_put_device(&core_dev->vdev);
}

struct pci_device_id nvidia_vgpu_vfio_table[] = {
	{
		.vendor      = PCI_VENDOR_ID_NVIDIA,
		.device      = PCI_ANY_ID,
		.subvendor   = PCI_ANY_ID,
		.subdevice   = PCI_ANY_ID,
		.class       = (PCI_CLASS_DISPLAY_3D << 8),
		.class_mask  = ~0,
	},
	{ }
};
MODULE_DEVICE_TABLE(pci, nvidia_vgpu_vfio_table);

struct pci_driver nvidia_vgpu_vfio_driver = {
	.name               = "nvidia-vgpu-vfio",
	.id_table           = nvidia_vgpu_vfio_table,
	.probe              = nvidia_vgpu_vfio_probe,
	.remove             = nvidia_vgpu_vfio_remove,
	.driver_managed_dma = true,
};

module_pci_driver(nvidia_vgpu_vfio_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Vinay Kabra <vkabra@nvidia.com>");
MODULE_AUTHOR("Kirti Wankhede <kwankhede@nvidia.com>");
MODULE_AUTHOR("Zhi Wang <zhiw@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA vGPU VFIO Variant Driver - User Level driver for NVIDIA vGPU");
