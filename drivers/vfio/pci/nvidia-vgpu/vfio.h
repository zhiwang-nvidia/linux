/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#ifndef _NVIDIA_VGPU_VFIO_H__
#define _NVIDIA_VGPU_VFIO_H__

#include <linux/debugfs.h>
#include <linux/vfio_pci_core.h>

#include "vgpu_mgr.h"

#define PCI_CONFIG_SPACE_LENGTH 4096

#define CAP_LIST_NEXT_PTR_MSIX 0x7c
#define MSIX_CAP_SIZE   0xc

struct nvidia_vgpu_vfio_log {
	struct debugfs_blob_wrapper blob;
	void *mem;
	struct dentry *dentry;
};

struct nvidia_vgpu_vfio {
	struct vfio_pci_core_device core_dev;
	u8 vconfig[PCI_CONFIG_SPACE_LENGTH];
	void __iomem *bar0_map;

	struct nvidia_vgpu_mgr *vgpu_mgr;
	struct nvidia_vgpu_type *vgpu_type;

	/* lock to protect vgpu pointer and following members */
	struct mutex vfio_vgpu_lock;
	struct nvidia_vgpu *vgpu;
	bool vdev_is_opened;
	bool driver_is_unbound;
	struct pid *task_pid;
	struct completion vdev_closing_completion;

	struct nvidia_vgpu_event_listener pf_driver_event_listener;
	struct nvidia_vgpu_event_listener pf_event_listener;

	/* Logs */
	struct nvidia_vgpu_vfio_log log_init_task;
	struct nvidia_vgpu_vfio_log log_vgpu_task;
	struct nvidia_vgpu_vfio_log log_kernel;
};

static inline struct nvidia_vgpu_vfio *core_dev_to_nvdev(struct vfio_pci_core_device *core_dev)
{
	return container_of(core_dev, struct nvidia_vgpu_vfio, core_dev);
}

void nvidia_vgpu_vfio_setup_config(struct nvidia_vgpu_vfio *nvdev);
ssize_t nvidia_vgpu_vfio_access(struct nvidia_vgpu_vfio *nvdev, char __user *buf, size_t count,
				loff_t ppos, bool iswrite);

int nvidia_vgpu_vfio_setup_sysfs(struct nvidia_vgpu_vfio *nvdev);
void nvidia_vgpu_vfio_clean_sysfs(struct nvidia_vgpu_vfio *nvdev);
int nvidia_vgpu_vfio_setup_debugfs(struct nvidia_vgpu_vfio *nvdev);
void nvidia_vgpu_vfio_clean_debugfs(struct nvidia_vgpu_vfio *nvdev);
void nvidia_vgpu_vfio_update_logs(struct nvidia_vgpu_vfio *nvdev);

#endif /* _NVIDIA_VGPU_VFIO_H__ */
