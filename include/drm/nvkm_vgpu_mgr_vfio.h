/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#ifndef __NVKM_VGPU_MGR_VFIO_H__
#define __NVKM_VGPU_MGR_VFIO_H__

struct nvidia_vgpu_vfio_handle_data {
	void *priv;
};

/* A combo of handles of RmClient and RmDevice */
struct nvidia_vgpu_gsp_client {
	void *gsp_client;
	void *gsp_device;
};

struct nvkm_vgpu_mgr_vfio_ops {
	bool (*vgpu_mgr_is_enabled)(void *handle);
	void (*get_handle)(void *handle,
		           struct nvidia_vgpu_vfio_handle_data *data);
	int (*attach_handle)(void *handle,
		             struct nvidia_vgpu_vfio_handle_data *data);
	void (*detach_handle)(void *handle);
	int (*alloc_gsp_client)(void *handle,
				struct nvidia_vgpu_gsp_client *client);
	void (*free_gsp_client)(struct nvidia_vgpu_gsp_client *client);
	u32 (*get_gsp_client_handle)(struct nvidia_vgpu_gsp_client *client);
};

struct nvkm_vgpu_mgr_vfio_ops *nvkm_vgpu_mgr_get_vfio_ops(void *handle);

#endif
