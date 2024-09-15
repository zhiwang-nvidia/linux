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

struct nvidia_vgpu_mem {
	u64 addr;
	u64 size;
	void * __iomem bar1_vaddr;
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
	void *(*rm_ctrl_get)(struct nvidia_vgpu_gsp_client *client,
			     u32 cmd, u32 size);
	int (*rm_ctrl_wr)(struct nvidia_vgpu_gsp_client *client,
			  void *ctrl);
	void *(*rm_ctrl_rd)(struct nvidia_vgpu_gsp_client *client, u32 cmd,
			    u32 size);
	void (*rm_ctrl_done)(struct nvidia_vgpu_gsp_client *client,
			     void *ctrl);
	int (*alloc_chids)(void *handle, int count);
	void (*free_chids)(void *handle, int offset, int count);
	struct nvidia_vgpu_mem *(*alloc_fbmem)(void *handle, u64 size,
					       bool vmmu_aligned);
	void (*free_fbmem)(struct nvidia_vgpu_mem *mem);
	int (*bar1_map_mem)(struct nvidia_vgpu_mem *mem);
	void (*bar1_unmap_mem)(struct nvidia_vgpu_mem *mem);
	void (*get_engine_bitmap)(void *handle, unsigned long *bitmap);
};

struct nvkm_vgpu_mgr_vfio_ops *nvkm_vgpu_mgr_get_vfio_ops(void *handle);

#endif
