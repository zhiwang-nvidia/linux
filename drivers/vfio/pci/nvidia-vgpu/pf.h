/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_PF_H__
#define __NVIDIA_VGPU_PF_H__

#include <linux/pci.h>
#include <drm/nvidia_vgpu_vfio_pf_intf.h>

struct nvidia_vgpu_mgr_handle {
	void *pf_drvdata;
	struct pci_dev *pf_pdev;
	struct nvidia_vgpu_vfio_ops *ops;
};

static inline int nvidia_vgpu_mgr_init_handle(struct pci_dev *pdev,
					      struct nvidia_vgpu_mgr_handle *h)
{
	struct pci_dev *pf_pdev;

	if (!pdev->is_virtfn)
		return -EINVAL;

	pf_pdev = pdev->physfn;

	h->ops = NULL;
	h->pf_pdev = pf_pdev;
	h->pf_drvdata = pci_get_drvdata(pf_pdev);

	if (strcmp(pf_pdev->driver->name, "NovaCore")) {
		pr_err("Cannot find an available PF driver!\n");
		return -EINVAL;
	}

	h->ops = nova_vgpu_get_vfio_ops(h->pf_drvdata);
	return 0;
}

#define nvidia_vgpu_mgr_support_is_enabled(h) ({ \
	typeof(h) __h = (h); \
	__h->ops->vgpu_is_enabled(__h->pf_drvdata); \
})

#define nvidia_vgpu_mgr_attach_handle(h, data) ({ \
	typeof(h) __h = (h); \
	__h->ops->attach_handle(__h->pf_drvdata, data); \
})

#define nvidia_vgpu_mgr_detach_handle(h) ({ \
	typeof(h) __h = (h); \
	__h->ops->detach_handle(__h->pf_drvdata); \
})

#define nvidia_vgpu_mgr_get_avail_chids(m) ({ \
	typeof(m) __m = (m); \
	__m->handle.ops->get_avail_chids(__m->handle.pf_drvdata); \
})

#define nvidia_vgpu_mgr_get_total_fbmem_size(m) ({ \
	typeof(m) __m = (m); \
	__m->handle.ops->get_total_fbmem_size(__m->handle.pf_drvdata); \
})

#define nvidia_vgpu_mgr_alloc_gsp_client(m, c) ({ \
	typeof(m) __m = (m); \
	__m->handle.ops->alloc_gsp_client(__m->handle.pf_drvdata, c); \
})

#define nvidia_vgpu_mgr_free_gsp_client(m, c) \
	((m)->handle.ops->free_gsp_client(c))

#define nvidia_vgpu_mgr_get_gsp_client_handle(m, c) \
	((m)->handle.ops->get_gsp_client_handle(c))

#define nvidia_vgpu_mgr_rm_ctrl_get(m, g, c, s) \
	((m)->handle.ops->rm_ctrl_get(g, c, s))

#define nvidia_vgpu_mgr_rm_ctrl_wr(m, g, c) \
	((m)->handle.ops->rm_ctrl_wr(g, c))

#define nvidia_vgpu_mgr_rm_ctrl_rd(m, g, c, s) \
	((m)->handle.ops->rm_ctrl_rd(g, c, s))

#define nvidia_vgpu_mgr_rm_ctrl_done(m, g, c) \
	((m)->handle.ops->rm_ctrl_done(g, c))

#define nvidia_vgpu_mgr_alloc_chids(m, o, s) ({ \
	typeof(m) __m = (m); \
	__m->handle.ops->alloc_chids(__m->handle.pf_drvdata, o, s); \
})

#define nvidia_vgpu_mgr_free_chids(m, o, s) ({ \
	typeof(m) __m = (m); \
	__m->handle.ops->free_chids(__m->handle.pf_drvdata, o, s); \
})

#define nvidia_vgpu_mgr_alloc_fbmem(m, info) ({\
	typeof(m) __m = (m); \
	__m->handle.ops->alloc_fbmem(__m->handle.pf_drvdata, info); \
})

#define nvidia_vgpu_mgr_free_fbmem(m, h) \
	((m)->handle.ops->free_fbmem(h))

#define nvidia_vgpu_mgr_bar1_map_mem(m, mem, info) \
	((m)->handle.ops->bar1_map_mem(mem, info))

#define nvidia_vgpu_mgr_bar1_unmap_mem(m, mem) \
	((m)->handle.ops->bar1_unmap_mem(mem))

#endif
