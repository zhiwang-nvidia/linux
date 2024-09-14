// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include "debug.h"
#include "vgpu_mgr.h"

static void vgpu_mgr_release(struct kref *kref)
{
	struct nvidia_vgpu_mgr *vgpu_mgr =
		container_of(kref, struct nvidia_vgpu_mgr, refcount);

	vgpu_mgr_debug(vgpu_mgr, "release\n");

	if (WARN_ON(atomic_read(&vgpu_mgr->num_vgpus)))
		return;

	kvfree(vgpu_mgr);
}

static void detach_vgpu_mgr(struct nvidia_vgpu_vfio_handle_data *handle_data)
{
	handle_data->vfio.private_data = NULL;
}

static void pf_detach_handle_fn(void *handle, struct nvidia_vgpu_vfio_handle_data *handle_data)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = handle_data->vfio.private_data;

	vgpu_mgr_debug(vgpu_mgr, "put\n");

	if (kref_put(&vgpu_mgr->refcount, vgpu_mgr_release))
		detach_vgpu_mgr(handle_data);
}

/**
 * nvidia_vgpu_mgr_release - release the vGPU manager
 * @vgpu_mgr: the vGPU manager to release.
 */
void nvidia_vgpu_mgr_release(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	if (!nvidia_vgpu_mgr_support_is_enabled(&vgpu_mgr->handle))
		return;

	nvidia_vgpu_mgr_detach_handle(&vgpu_mgr->handle);
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_release);

static struct nvidia_vgpu_mgr *alloc_vgpu_mgr(struct nvidia_vgpu_mgr_handle *handle)
{
	struct nvidia_vgpu_mgr *vgpu_mgr;

	vgpu_mgr = kvzalloc(sizeof(*vgpu_mgr), GFP_KERNEL);
	if (!vgpu_mgr)
		return ERR_PTR(-ENOMEM);

	vgpu_mgr->handle = *handle;

	kref_init(&vgpu_mgr->refcount);
	mutex_init(&vgpu_mgr->vgpu_list_lock);
	INIT_LIST_HEAD(&vgpu_mgr->vgpu_list_head);
	atomic_set(&vgpu_mgr->num_vgpus, 0);

	return vgpu_mgr;
}

static const char *pf_events_string[NVIDIA_VGPU_PF_EVENT_MAX] = {
	[NVIDIA_VGPU_PF_DRIVER_EVENT_SRIOV_CONFIGURE] = "SRIOV configure",
	[NVIDIA_VGPU_PF_DRIVER_EVENT_DRIVER_UNBIND] = "driver unbind",
};

static int pf_event_notify_fn(void *priv, unsigned int event, void *data)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = priv;

	if (WARN_ON(event >= NVIDIA_VGPU_PF_EVENT_MAX))
		return -EINVAL;

	vgpu_mgr_debug(vgpu_mgr, "handle PF event %s\n", pf_events_string[event]);

	/* more to come. */
	return 0;
}

static void attach_vgpu_mgr(struct nvidia_vgpu_mgr *vgpu_mgr,
			    struct nvidia_vgpu_vfio_handle_data *handle_data)
{
	handle_data->vfio.handle = vgpu_mgr->handle.pf_drvdata;
	handle_data->vfio.module = THIS_MODULE;
	handle_data->vfio.private_data = vgpu_mgr;
	handle_data->vfio.pf_event_notify_fn = pf_event_notify_fn;
	handle_data->vfio.pf_detach_handle_fn = pf_detach_handle_fn;
}

static int init_vgpu_mgr(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	vgpu_mgr->total_avail_chids = nvidia_vgpu_mgr_get_avail_chids(vgpu_mgr);
	vgpu_mgr->total_fbmem_size = nvidia_vgpu_mgr_get_total_fbmem_size(vgpu_mgr);

	vgpu_mgr_debug(vgpu_mgr, "total avail chids %u\n", vgpu_mgr->total_avail_chids);
	vgpu_mgr_debug(vgpu_mgr, "total fbmem size 0x%llx\n", vgpu_mgr->total_fbmem_size);

	return 0;
}

static int setup_pf_driver_caps(struct nvidia_vgpu_mgr *vgpu_mgr, unsigned long *caps)
{
	/* more to come */
	return 0;
}

static int pf_attach_handle_fn(void *handle, struct nvidia_vgpu_vfio_handle_data *handle_data,
			       struct nvidia_vgpu_vfio_attach_handle_data *attach_data)
{
	struct nvidia_vgpu_mgr *vgpu_mgr;
	int ret;

	/* PF driver is unbinding */
	if (handle_data->pf.driver_is_unbound)
		return -ENODEV;

	if (handle_data->vfio.private_data) {
		vgpu_mgr = handle_data->vfio.private_data;

		ret = attach_data->init_vfio_fn(vgpu_mgr, attach_data->init_vfio_fn_data);
		if (ret)
			return ret;

		kref_get(&vgpu_mgr->refcount);
		vgpu_mgr_debug(vgpu_mgr, "use existing %px\n", vgpu_mgr);
		return 0;
	}

	vgpu_mgr = alloc_vgpu_mgr(attach_data->vgpu_mgr_handle);
	if (IS_ERR(vgpu_mgr))
		return PTR_ERR(vgpu_mgr);

	ret = setup_pf_driver_caps(vgpu_mgr, handle_data->pf.driver_caps);
	if (ret)
		goto fail_setup_pf_driver_caps;

	ret = init_vgpu_mgr(vgpu_mgr);
	if (ret)
		goto fail_init_vgpu_mgr;

	attach_vgpu_mgr(vgpu_mgr, handle_data);

	ret = attach_data->init_vfio_fn(vgpu_mgr, attach_data->init_vfio_fn_data);
	if (ret)
		goto fail_init_fn;

	vgpu_mgr_debug(vgpu_mgr, "created new %px\n", vgpu_mgr);

	return 0;

fail_init_fn:
	detach_vgpu_mgr(handle_data);
fail_init_vgpu_mgr:
fail_setup_pf_driver_caps:
	kvfree(vgpu_mgr);
	return ret;
}

/**
 * nvidia_vgpu_mgr_setup - setup the vGPU manager
 * @dev: the VF pci_dev.
 * @init_fn: the init function of VFIO interfaces
 * @init_fn_data: the init function data of VFIO interfaces
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_mgr_setup(struct pci_dev *dev, int (*init_vfio_fn)(void *priv, void *data),
			  void *init_vfio_fn_data)
{
	struct nvidia_vgpu_mgr_handle handle = {0};
	struct nvidia_vgpu_vfio_attach_handle_data attach_handle_data;
	int ret;

	ret = nvidia_vgpu_mgr_init_handle(dev, &handle);
	if (ret)
		return ret;

	if (!nvidia_vgpu_mgr_support_is_enabled(&handle))
		return -ENODEV;

	attach_handle_data.pf_attach_handle_fn = pf_attach_handle_fn;
	attach_handle_data.init_vfio_fn = init_vfio_fn;
	attach_handle_data.init_vfio_fn_data = init_vfio_fn_data;
	attach_handle_data.vgpu_mgr_handle = &handle;

	return nvidia_vgpu_mgr_attach_handle(&handle, &attach_handle_data);
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_setup);
