/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_MGR_NVKM_H__
#define __NVIDIA_VGPU_MGR_NVKM_H__

#include <linux/pci.h>
#include <drm/nvkm_vgpu_mgr_vfio.h>

struct nvidia_vgpu_mgr_handle {
	void *pf_drvdata;
	struct nvkm_vgpu_mgr_vfio_ops *ops;
	struct nvidia_vgpu_vfio_handle_data data;
};

static inline int nvidia_vgpu_mgr_get_handle(struct pci_dev *pdev,
		struct nvidia_vgpu_mgr_handle *h)
{
	struct pci_dev *pf_dev;

	if (!pdev->is_virtfn)
		return -EINVAL;

	pf_dev = pdev->physfn;

	if (strcmp(pf_dev->driver->name, "nvkm"))
		return -EINVAL;

	h->pf_drvdata = pci_get_drvdata(pf_dev);
	h->ops = nvkm_vgpu_mgr_get_vfio_ops(h->pf_drvdata);
	h->ops->get_handle(h->pf_drvdata, &h->data);

	return 0;
}

#define nvidia_vgpu_mgr_support_is_enabled(h) \
	(h).ops->vgpu_mgr_is_enabled((h).pf_drvdata)

#define nvidia_vgpu_mgr_attach_handle(h) \
	(h)->ops->attach_handle((h)->pf_drvdata, &(h)->data)

#define nvidia_vgpu_mgr_detach_handle(h) \
	(h)->ops->detach_handle((h)->pf_drvdata)

#endif
