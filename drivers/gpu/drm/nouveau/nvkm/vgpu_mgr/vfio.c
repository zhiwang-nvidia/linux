/* SPDX-License-Identifier: MIT */

#include <core/device.h>

#include <vgpu_mgr/vgpu_mgr.h>
#include <drm/nvkm_vgpu_mgr_vfio.h>

static bool vgpu_mgr_is_enabled(void *handle)
{
	struct nvkm_device *device = handle;

	return nvkm_vgpu_mgr_is_enabled(device);
}

static void get_handle(void *handle,
		       struct nvidia_vgpu_vfio_handle_data *data)
{
	struct nvkm_device *device = handle;
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	if (vgpu_mgr->vfio_handle_data.priv)
		memcpy(data, &vgpu_mgr->vfio_handle_data, sizeof(*data));
}

static void detach_handle(void *handle)
{
	struct nvkm_device *device = handle;
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	vgpu_mgr->vfio_handle_data.priv = NULL;
}

static int attach_handle(void *handle,
			 struct nvidia_vgpu_vfio_handle_data *data)
{
	struct nvkm_device *device = handle;
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	if (vgpu_mgr->vfio_handle_data.priv)
		return -EEXIST;

	memcpy(&vgpu_mgr->vfio_handle_data, data, sizeof(*data));
	return 0;
}

struct nvkm_vgpu_mgr_vfio_ops nvkm_vgpu_mgr_vfio_ops = {
	.vgpu_mgr_is_enabled = vgpu_mgr_is_enabled,
	.get_handle = get_handle,
	.attach_handle = attach_handle,
	.detach_handle = detach_handle,
};

/**
 * nvkm_vgpu_mgr_init_vfio_ops - init the callbacks for VFIO
 * @vgpu_mgr: the nvkm vGPU manager
 */
void nvkm_vgpu_mgr_init_vfio_ops(struct nvkm_vgpu_mgr *vgpu_mgr)
{
	vgpu_mgr->vfio_ops = &nvkm_vgpu_mgr_vfio_ops;
}

struct nvkm_vgpu_mgr_vfio_ops *nvkm_vgpu_mgr_get_vfio_ops(void *handle)
{
	struct nvkm_device *device = handle;
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	return vgpu_mgr->vfio_ops;
}
EXPORT_SYMBOL(nvkm_vgpu_mgr_get_vfio_ops);
