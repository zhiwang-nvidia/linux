/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#include "vgpu_mgr.h"

DEFINE_MUTEX(vgpu_mgr_attach_lock);

static void unmap_pf_mmio(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	iounmap(vgpu_mgr->bar0_vaddr);
}

static int map_pf_mmio(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	struct pci_dev *pdev = vgpu_mgr->pdev;
	resource_size_t start, size;
	void *vaddr;

	start = pci_resource_start(pdev, 0);
	size = pci_resource_len(pdev, 0);

	vaddr = ioremap(start, size);
	if (!vaddr)
		return -ENOMEM;

	vgpu_mgr->bar0_vaddr = vaddr;

	return 0;
}

static void vgpu_mgr_release(struct kref *kref)
{
	struct nvidia_vgpu_mgr *vgpu_mgr =
		container_of(kref, struct nvidia_vgpu_mgr, refcount);

	unmap_pf_mmio(vgpu_mgr);
	nvidia_vgpu_mgr_free_gsp_client(vgpu_mgr, &vgpu_mgr->gsp_client);
	nvidia_vgpu_mgr_detach_handle(&vgpu_mgr->handle);
	kvfree(vgpu_mgr);
}

/**
 * nvidia_vgpu_mgr_put - put the vGPU manager
 * @vgpu: the vGPU manager to put.
 *
 */
void nvidia_vgpu_mgr_put(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	if (!nvidia_vgpu_mgr_support_is_enabled(vgpu_mgr->handle))
		return;

	mutex_lock(&vgpu_mgr_attach_lock);
	kref_put(&vgpu_mgr->refcount, vgpu_mgr_release);
	mutex_unlock(&vgpu_mgr_attach_lock);
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_put);

/**
 * nvidia_vgpu_mgr_get - get the vGPU manager
 * @dev: the VF pci_dev.
 *
 * Returns: pointer to vgpu_mgr on success, IS_ERR() on failure.
 */
struct nvidia_vgpu_mgr *nvidia_vgpu_mgr_get(struct pci_dev *dev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr;
	struct nvidia_vgpu_mgr_handle handle;
	int ret;

	mutex_lock(&vgpu_mgr_attach_lock);

	memset(&handle, 0, sizeof(handle));

	ret = nvidia_vgpu_mgr_get_handle(dev, &handle);
	if (ret) {
		mutex_unlock(&vgpu_mgr_attach_lock);
		return ERR_PTR(ret);
	}

	if (!nvidia_vgpu_mgr_support_is_enabled(handle)) {
		mutex_unlock(&vgpu_mgr_attach_lock);
		return ERR_PTR(-ENODEV);
	}

	if (handle.data.priv) {
		vgpu_mgr = handle.data.priv;
		kref_get(&vgpu_mgr->refcount);
		mutex_unlock(&vgpu_mgr_attach_lock);
		return vgpu_mgr;
	}

	vgpu_mgr = kvzalloc(sizeof(*vgpu_mgr), GFP_KERNEL);
	if (!vgpu_mgr) {
		ret = -ENOMEM;
		goto fail_alloc_vgpu_mgr;
	}

	vgpu_mgr->handle = handle;
	vgpu_mgr->handle.data.priv = vgpu_mgr;

	ret = nvidia_vgpu_mgr_attach_handle(&handle);
	if (ret)
		goto fail_attach_handle;

	kref_init(&vgpu_mgr->refcount);
	mutex_init(&vgpu_mgr->vgpu_id_lock);

	vgpu_mgr->pdev = dev->physfn;

	ret = nvidia_vgpu_mgr_alloc_gsp_client(vgpu_mgr,
					       &vgpu_mgr->gsp_client);
	if (ret)
		goto fail_alloc_gsp_client;

	ret = nvidia_vgpu_mgr_init_vgpu_types(vgpu_mgr);
	if (ret)
		goto fail_init_vgpu_types;

	ret = map_pf_mmio(vgpu_mgr);
	if (ret)
		goto fail_map_pf_mmio;

	mutex_unlock(&vgpu_mgr_attach_lock);
	return vgpu_mgr;

fail_map_pf_mmio:
fail_init_vgpu_types:
	nvidia_vgpu_mgr_free_gsp_client(vgpu_mgr, &vgpu_mgr->gsp_client);
fail_alloc_gsp_client:
	nvidia_vgpu_mgr_detach_handle(&vgpu_mgr->handle);
fail_attach_handle:
	kvfree(vgpu_mgr);
fail_alloc_vgpu_mgr:
	mutex_unlock(&vgpu_mgr_attach_lock);
	vgpu_mgr = ERR_PTR(ret);
	return vgpu_mgr;
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_get);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Zhi Wang <zhiw@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA VGPU manager - core module to support VFIO PCI driver for NVIDIA vGPU");
