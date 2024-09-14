/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#ifndef __NVKM_VGPU_MGR_VFIO_H__
#define __NVKM_VGPU_MGR_VFIO_H__

struct nvidia_vgpu_vfio_handle_data {
	void *priv;
};

struct nvkm_vgpu_mgr_vfio_ops {
	bool (*vgpu_mgr_is_enabled)(void *handle);
	void (*get_handle)(void *handle,
		           struct nvidia_vgpu_vfio_handle_data *data);
	int (*attach_handle)(void *handle,
		             struct nvidia_vgpu_vfio_handle_data *data);
	void (*detach_handle)(void *handle);
};

struct nvkm_vgpu_mgr_vfio_ops *nvkm_vgpu_mgr_get_vfio_ops(void *handle);

#endif
