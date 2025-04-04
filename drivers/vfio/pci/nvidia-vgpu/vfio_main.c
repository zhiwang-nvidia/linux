// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vfio_pci_core.h>
#include <linux/types.h>

#include "debug.h"
#include "vfio.h"

static inline struct vfio_pci_core_device *vdev_to_core_dev(struct vfio_device *vdev)
{
	return container_of(vdev, struct vfio_pci_core_device, vdev);
}

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
	struct nvidia_vgpu_type *type = nvdev->vgpu_type;
	struct nvidia_vgpu *vgpu;
	int ret;

	if (WARN_ON(!type || !nvdev->task_pid))
		return -ENODEV;

	vgpu = kzalloc(sizeof(*vgpu), GFP_KERNEL);
	if (!vgpu)
		return -ENOMEM;

	vgpu->info.id = pci_iov_vf_id(pdev);
	vgpu->info.dbdf = (0 << 16) | pci_dev_id(pdev);
	vgpu->info.gfid = pdev_to_gfid(pdev);
	vgpu->info.vgpu_type = type;
	vgpu->info.vm_pid = pid_nr(nvdev->task_pid);

	vgpu->vgpu_mgr = vgpu_mgr;
	vgpu->pdev = pdev;

	ret = nvidia_vgpu_mgr_create_vgpu(vgpu);
	if (ret) {
		kfree(vgpu);
		return ret;
	}

	nvdev->vgpu = vgpu;
	return 0;
}

static inline bool pdev_is_present(struct pci_dev *pdev)
{
	struct pci_dev *physfn = (pdev->is_virtfn) ? pdev->physfn : pdev;

	if (pdev->is_virtfn)
		return (pci_device_is_present(physfn) &&
				pdev->error_state != pci_channel_io_perm_failure);
	else
		return pci_device_is_present(physfn);
}

/* Wait till 1000 ms for HW that returns CRS completion status */
#define MIN_FLR_WAIT_TIME 100
#define MAX_FLR_WAIT_TIME 1000

static int do_vf_flr(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;
	u32 data, elapsed_time = 0;

	if (!pdev->is_virtfn)
		return 0;

	if (!pdev_is_present(pdev))
		return -ENOTTY;

	pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP, &data);
	if (!(data & PCI_EXP_DEVCAP_FLR)) {
		nvdev_error(nvdev, "FLR capability not present on the VF.\n");
		return -EINVAL;
	}

	device_lock(&pdev->dev);
	pci_set_power_state(pdev, PCI_D0);
	pci_save_state(pdev);

	if (!pci_wait_for_pending_transaction(pdev))
		nvdev_error(nvdev, "Timed out waiting for transaction pending to go to 0.\n");

	pcie_capability_set_word(pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_BCR_FLR);

	/*
	 * If CRS-SV is supported and enabled, then the root-port returns '0001h'
	 * for a PCI config read of the 16-byte vendor_id field. This indicates CRS
	 * completion status.
	 *
	 * If CRS-SV is not supported/enabled, then the root-port will generally
	 * synthesise ~0 data for any PCI config read.
	 */
	do {
		msleep(MIN_FLR_WAIT_TIME);
		elapsed_time += MIN_FLR_WAIT_TIME;

		pci_read_config_dword(pdev, PCI_VENDOR_ID, &data);
	} while (((data & 0xffff) == 0x0001) && (elapsed_time < MAX_FLR_WAIT_TIME));

	if (elapsed_time < MAX_FLR_WAIT_TIME) {
		/*
		 * Device is back from the CRS-SV, continue checking
		 * if device is ready by reading PCI_COMMAND.
		 */
		do {
			pci_read_config_dword(pdev, PCI_COMMAND, &data);
			if (data != ~0)
				goto flr_done;

			msleep(MIN_FLR_WAIT_TIME);
			elapsed_time += MIN_FLR_WAIT_TIME;
		} while (elapsed_time < MAX_FLR_WAIT_TIME);

		nvdev_error(nvdev, "FLR failed non-CRS case, waited for %d ms\n", elapsed_time);
	} else {
		nvdev_error(nvdev, "FLR failed CRS case, waited for %d ms\n", elapsed_time);
	}

	/* Device is not usable. */
	xchg(&pdev->error_state, pci_channel_io_perm_failure);
	device_unlock(&pdev->dev);
	return -ENOTTY;

flr_done:
	pci_restore_state(pdev);
	device_unlock(&pdev->dev);

	return 0;
}

static int nvidia_vgpu_vfio_open_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;
	u64 pf_dma_mask;
	int ret;

	nvdev_debug(nvdev, "open device\n");

	mutex_lock(&nvdev->vfio_vgpu_lock);
	if (!nvdev->vgpu_type) {
		nvdev_error(nvdev, "a vGPU type must be chosen before opening VFIO device\n");
		ret = -ENODEV;
		goto err_unlock;
	}

	if (nvdev->driver_is_unbound) {
		nvdev_error(nvdev, "the driver has been torn down because PF driver is unbound "
				   "or the admin is disabling the VF\n");
		ret = -ENODEV;
		goto err_unlock;
	}

	if (nvdev->vdev_is_opened) {
		ret = -EBUSY;
		goto err_unlock;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_unlock;

	pci_set_master(pdev);

	pf_dma_mask = dma_get_mask(&pdev->physfn->dev);
	dma_set_mask(&pdev->dev, pf_dma_mask);
	dma_set_coherent_mask(&pdev->dev, pf_dma_mask);

	ret = do_vf_flr(vdev);
	if (ret)
		goto err_reset_function;

	nvdev->task_pid = get_task_pid(current, PIDTYPE_PID);

	ret = create_vgpu(nvdev);
	if (ret)
		goto err_create_vgpu;

	ret = nvidia_vgpu_mgr_set_bme(nvdev->vgpu, true);
	if (ret)
		goto err_enable_bme;

	nvidia_vgpu_vfio_setup_config(nvdev);

	nvdev->vdev_is_opened = true;
	reinit_completion(&nvdev->vdev_closing_completion);

	nvdev_debug(nvdev, "VFIO device is opened, client pid: %u\n", pid_nr(nvdev->task_pid));

	mutex_unlock(&nvdev->vfio_vgpu_lock);
	return 0;

err_enable_bme:
	destroy_vgpu(nvdev);
err_create_vgpu:
	put_pid(nvdev->task_pid);
err_reset_function:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
err_unlock:
	mutex_unlock(&nvdev->vfio_vgpu_lock);
	return ret;
}

static void nvidia_vgpu_vfio_close_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct pci_dev *pdev = core_dev->pdev;

	nvdev_debug(nvdev, "VFIO device is closing, client pid: %u\n", pid_nr(nvdev->task_pid));

	mutex_lock(&nvdev->vfio_vgpu_lock);

	if (nvdev->bar0_map) {
		iounmap(nvdev->bar0_map);
		pci_release_selected_regions(pdev, 1 << 0);
		nvdev->bar0_map = NULL;
	}

	destroy_vgpu(nvdev);

	put_pid(nvdev->task_pid);
	nvdev->task_pid = NULL;

	pci_clear_master(pdev);
	pci_disable_device(pdev);

	nvdev->vdev_is_opened = false;
	complete(&nvdev->vdev_closing_completion);

	mutex_unlock(&nvdev->vfio_vgpu_lock);

	nvdev_debug(nvdev, "VFIO device is closed\n");
}

static int get_region_info(struct vfio_pci_core_device *core_dev, unsigned long arg)
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
		info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
		info.size = pci_resource_len(pdev, info.index);

		if (info.index == VFIO_PCI_BAR1_REGION_INDEX)
			info.size = nvdev->vgpu->info.vgpu_type->bar1_length * SZ_1M;

		if (!info.size) {
			info.flags = 0;
			break;
		}
		info.flags = VFIO_REGION_INFO_FLAG_READ |
			VFIO_REGION_INFO_FLAG_WRITE |
			VFIO_REGION_INFO_FLAG_MMAP;
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

static long nvidia_vgpu_vfio_ioctl(struct vfio_device *vdev, unsigned int cmd, unsigned long arg)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	int ret = 0;

	if (WARN_ON(!nvdev->vgpu || !nvdev->vdev_is_opened))
		return -ENODEV;

	switch (cmd) {
	case VFIO_DEVICE_GET_REGION_INFO:
		ret = get_region_info(core_dev, arg);
		break;
	case VFIO_DEVICE_GET_PCI_HOT_RESET_INFO:
	case VFIO_DEVICE_PCI_HOT_RESET:
		break;
	case VFIO_DEVICE_RESET:
		ret = nvidia_vgpu_mgr_reset_vgpu(nvdev->vgpu);
		break;
	default:
		ret = vfio_pci_core_ioctl(vdev, cmd, arg);
		break;
	}
	return ret;
}

static ssize_t nvidia_vgpu_vfio_read(struct vfio_device *vdev, char __user *buf, size_t count,
				     loff_t *ppos)
{
	struct vfio_pci_core_device *core_dev = vdev_to_core_dev(vdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	u64 val;
	size_t done = 0;
	int ret = 0, size;

	if (WARN_ON(!nvdev->vgpu || !nvdev->vdev_is_opened))
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

	if (WARN_ON(!nvdev->vgpu || !nvdev->vdev_is_opened))
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

	if (WARN_ON(!nvdev->vgpu || !nvdev->vdev_is_opened))
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

	if ((req_start + req_len > phys_len) || phys_len == 0)
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

static void clean_nvdev_unbound(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;

	/* driver unbound path is called from the event chain. */
	lockdep_assert_held(&vgpu_mgr->pf_driver_event_chain.lock);
	list_del_init(&nvdev->pf_driver_event_listener.list);

	nvidia_vgpu_vfio_clean_sysfs(nvdev);

	nvidia_vgpu_mgr_release(nvdev->vgpu_mgr);
	nvdev->vgpu_mgr = NULL;
	nvdev->vgpu_type = NULL;
}

static void handle_driver_unbound(struct nvidia_vgpu_vfio *nvdev)
{
	struct task_struct *task;

	mutex_lock(&nvdev->vfio_vgpu_lock);

	if (nvdev->driver_is_unbound) {
		mutex_unlock(&nvdev->vfio_vgpu_lock);
		return;
	}

	nvdev->driver_is_unbound = true;

	if (nvdev->vdev_is_opened) {
		task = get_pid_task(nvdev->task_pid, PIDTYPE_PID);
		if (!task) {
			mutex_unlock(&nvdev->vfio_vgpu_lock);
			return;
		}

		nvdev_debug(nvdev, "Killing client pid: %u\n", pid_nr(nvdev->task_pid));

		send_sig(SIGTERM, task, 1);
		put_task_struct(task);

		mutex_unlock(&nvdev->vfio_vgpu_lock);

		wait_for_completion(&nvdev->vdev_closing_completion);
	} else {
		mutex_unlock(&nvdev->vfio_vgpu_lock);
	}

	clean_nvdev_unbound(nvdev);
}

static int handle_pf_driver_event(struct nvidia_vgpu_event_listener *self, unsigned int event,
				  void *p)
{
	struct nvidia_vgpu_vfio *nvdev = container_of(self, struct nvidia_vgpu_vfio,
			pf_driver_event_listener);
	struct pci_dev *pdev = nvdev->core_dev.pdev;

	switch (event) {
	case NVIDIA_VGPU_PF_DRIVER_EVENT_DRIVER_UNBIND:
		nvdev_debug(nvdev, "handle PF event driver unbind\n");

		handle_driver_unbound(nvdev);
		break;
	case NVIDIA_VGPU_PF_DRIVER_EVENT_SRIOV_CONFIGURE:
		int num_vfs = *(int *)p;

		nvdev_debug(nvdev, "handle PF event SRIOV configure\n");

		if (!num_vfs) {
			handle_driver_unbound(nvdev);
		} else {
			/* convert num_vfs to max VF ID */
			num_vfs--;
			if (pci_iov_vf_id(pdev) > num_vfs)
				handle_driver_unbound(nvdev);
		}
		break;
	}
	return 0;
}

static void register_pf_driver_event_listener(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;

	nvdev->pf_driver_event_listener.func = handle_pf_driver_event;
	INIT_LIST_HEAD(&nvdev->pf_driver_event_listener.list);

	nvidia_vgpu_event_register_listener(&vgpu_mgr->pf_driver_event_chain,
					    &nvdev->pf_driver_event_listener);
}

static void unregister_pf_driver_event_listener(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;

	nvidia_vgpu_event_unregister_listener(&vgpu_mgr->pf_driver_event_chain,
					      &nvdev->pf_driver_event_listener);
}

static void clean_nvdev(struct nvidia_vgpu_vfio *nvdev)
{
	if (nvdev->driver_is_unbound)
		return;

	unregister_pf_driver_event_listener(nvdev);
	nvidia_vgpu_vfio_clean_sysfs(nvdev);

	nvidia_vgpu_mgr_release(nvdev->vgpu_mgr);
	nvdev->vgpu_mgr = NULL;
	nvdev->vgpu_type = NULL;
}

static int setup_nvdev(void *priv, void *data)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = priv;
	struct nvidia_vgpu_vfio *nvdev = data;
	int ret;

	mutex_init(&nvdev->vfio_vgpu_lock);
	init_completion(&nvdev->vdev_closing_completion);

	nvdev->vgpu_mgr = vgpu_mgr;

	ret = nvidia_vgpu_vfio_setup_sysfs(nvdev);
	if (ret)
		return ret;

	register_pf_driver_event_listener(nvdev);
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

	ret = nvidia_vgpu_mgr_setup(pdev, setup_nvdev, nvdev);
	if (ret)
		goto err_setup_vgpu_mgr;

	dev_set_drvdata(&pdev->dev, &nvdev->core_dev);

	ret = vfio_pci_core_register_device(&nvdev->core_dev);
	if (ret)
		goto err_register_core_device;

	return 0;

err_register_core_device:
	clean_nvdev(nvdev);
err_setup_vgpu_mgr:
	vfio_put_device(&nvdev->core_dev.vdev);
	pci_err(pdev, "VF probe failed with ret: %d\n", ret);
	return ret;
}

static void nvidia_vgpu_vfio_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_dev = dev_get_drvdata(&pdev->dev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);

	WARN_ON(nvdev->vgpu || nvdev->vdev_is_opened);

	vfio_pci_core_unregister_device(core_dev);
	clean_nvdev(nvdev);
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vinay Kabra <vkabra@nvidia.com>");
MODULE_AUTHOR("Kirti Wankhede <kwankhede@nvidia.com>");
MODULE_AUTHOR("Zhi Wang <zhiw@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA vGPU VFIO Variant Driver - User Level driver for NVIDIA vGPU");
