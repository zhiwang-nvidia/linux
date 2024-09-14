/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VGPU_MGR_H__
#define __NVKM_VGPU_MGR_H__

#include <drm/nvkm_vgpu_mgr_vfio.h>

#define NVIDIA_MAX_VGPUS 2

struct nvkm_vgpu_mgr {
	bool enabled;
	struct nvkm_device *nvkm_dev;

	const struct nvif_driver *driver;

	const struct nvif_client_impl *cli_impl;
	struct nvif_client_priv *cli_priv;

	const struct nvif_device_impl *dev_impl;
	struct nvif_device_priv *dev_priv;

	u64 vmmu_segment_size;

	void *vfio_ops;
	struct nvidia_vgpu_vfio_handle_data vfio_handle_data;
};

bool nvkm_vgpu_mgr_is_supported(struct nvkm_device *device);
bool nvkm_vgpu_mgr_is_enabled(struct nvkm_device *device);
int nvkm_vgpu_mgr_init(struct nvkm_device *device);
void nvkm_vgpu_mgr_fini(struct nvkm_device *device);
void nvkm_vgpu_mgr_populate_gsp_vf_info(struct nvkm_device *device,
					void *info);
void nvkm_vgpu_mgr_init_vfio_ops(struct nvkm_vgpu_mgr *vgpu_mgr);

#endif
