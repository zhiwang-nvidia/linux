/* SPDX-License-Identifier: MIT */

#include <core/device.h>
#include <subdev/gsp.h>

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

static int alloc_gsp_client(void *handle,
			    struct nvidia_vgpu_gsp_client *client)
{
	struct nvkm_device *device = handle;
	struct nvkm_gsp *gsp = device->gsp;
	int ret = -ENOMEM;

	client->gsp_device = kzalloc(sizeof(struct nvkm_gsp_device),
				     GFP_KERNEL);
	if (!client->gsp_device)
		return ret;

	client->gsp_client = kzalloc(sizeof(struct nvkm_gsp_client),
				     GFP_KERNEL);
	if (!client->gsp_client)
		goto fail_alloc_client;

	ret = nvkm_gsp_client_device_ctor(gsp, client->gsp_client,
					  client->gsp_device);
	if (ret)
		goto fail_client_device_ctor;

	return 0;

fail_client_device_ctor:
	kfree(client->gsp_client);
	client->gsp_client = NULL;

fail_alloc_client:
	kfree(client->gsp_device);
	client->gsp_device = NULL;

	return ret;
}

static void free_gsp_client(struct nvidia_vgpu_gsp_client *client)
{
	nvkm_gsp_device_dtor(client->gsp_device);
	nvkm_gsp_client_dtor(client->gsp_client);

	kfree(client->gsp_device);
	client->gsp_device = NULL;

	kfree(client->gsp_client);
	client->gsp_client = NULL;
}

static u32 get_gsp_client_handle(struct nvidia_vgpu_gsp_client *client)
{
	struct nvkm_gsp_client *c = client->gsp_client;

	return c->object.handle;
}

struct nvkm_vgpu_mgr_vfio_ops nvkm_vgpu_mgr_vfio_ops = {
	.vgpu_mgr_is_enabled = vgpu_mgr_is_enabled,
	.get_handle = get_handle,
	.attach_handle = attach_handle,
	.detach_handle = detach_handle,
	.alloc_gsp_client = alloc_gsp_client,
	.free_gsp_client = free_gsp_client,
	.get_gsp_client_handle = get_gsp_client_handle,
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
