/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#ifndef __NVIDIA_VGPU_DEBUG_H__
#define __NVIDIA_VGPU_DEBUG_H__

#define vgpu_mgr_debug(v, f, a...) \
	pci_dbg((v)->handle.pf_pdev, "nvidia-vgpu-mgr: "f, ##a)

#define vgpu_debug(v, f, a...) ({ \
	typeof(v) __v = (v); \
	pci_dbg(__v->pdev, "nvidia-vgpu %d: "f, __v->info.id, ##a); \
})

#endif
