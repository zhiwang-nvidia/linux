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

#endif
