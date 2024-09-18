/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#ifndef __NVIDIA_VGPU_DEBUG_H__
#define __NVIDIA_VGPU_DEBUG_H__

#define nv_vgpu_dbg(v, f, a...) \
	pci_dbg(v->pdev, "nvidia-vgpu %d: "f, v->info.id, ##a)

#define nv_vgpu_info(v, f, a...) \
	pci_info(v->pdev, "nvidia-vgpu %d: "f, v->info.id, ##a)

#define nv_vgpu_err(v, f, a...) \
	pci_err(v->pdev, "nvidia-vgpu %d: "f, v->info.id, ##a)

#endif
