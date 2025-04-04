/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#ifndef __NVIDIA_VGPU_DEBUG_H__
#define __NVIDIA_VGPU_DEBUG_H__

#define vgpu_mgr_debug(v, f, a...) \
	pci_dbg((v)->handle.pf_pdev, "nvidia-vgpu-mgr: "f, ##a)

#define vgpu_mgr_error(v, f, a...) \
	pci_err((v)->handle.pf_pdev, "nvidia-vgpu-mgr: "f, ##a)

#define vgpu_debug(v, f, a...) ({ \
	typeof(v) __v = (v); \
	pci_dbg(__v->pdev, "nvidia-vgpu %d: "f, __v->info.id, ##a); \
})

#define vgpu_error(v, f, a...) ({ \
	typeof(v) __v = (v); \
	pci_err(__v->pdev, "nvidia-vgpu %d: "f, __v->info.id, ##a); \
})

#define nvdev_debug(n, f, a...) ({ \
	typeof(n) __n = (n); \
	pci_dbg(__n->core_dev.pdev, "nvidia-vgpu-vfio: "f, ##a); \
})

#define nvdev_error(n, f, a...) ({ \
	typeof(n) __n = (n); \
	pci_err(__n->core_dev.pdev, "nvidia-vgpu-vfio: "f, ##a); \
})

#endif
