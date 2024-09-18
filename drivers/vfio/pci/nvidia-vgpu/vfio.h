/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#ifndef _NVIDIA_VGPU_VFIO_H__
#define _NVIDIA_VGPU_VFIO_H__

#include <linux/vfio_pci_core.h>

#include <nvrm/nvtypes.h>
#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrla081.h>
#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080vgpumgrinternal.h>

#include "vgpu_mgr.h"

#define VGPU_CONFIG_PARAMS_MAX_LENGTH 1024
#define DEVICE_CLASS_LENGTH 5
#define PCI_CONFIG_SPACE_LENGTH 4096

#define CAP_LIST_NEXT_PTR_MSIX 0x7c
#define MSIX_CAP_SIZE   0xc

struct nvidia_vgpu_vfio {
	struct vfio_pci_core_device core_dev;
	u8 config_space[PCI_CONFIG_SPACE_LENGTH];

	void __iomem *bar0_map;

	u8 **vgpu_types;
	NVA081_CTRL_VGPU_INFO *curr_vgpu_type;
	u32 num_vgpu_types;

	struct nvidia_vgpu_mgr *vgpu_mgr;
	struct nvidia_vgpu *vgpu;
};

void nvidia_vgpu_vfio_setup_config(struct nvidia_vgpu_vfio *nvdev);
ssize_t nvidia_vgpu_vfio_access(struct nvidia_vgpu_vfio *nvdev,
				char __user *buf, size_t count,
				loff_t ppos, bool iswrite);

#endif /* _NVIDIA_VGPU_VFIO_H__ */
