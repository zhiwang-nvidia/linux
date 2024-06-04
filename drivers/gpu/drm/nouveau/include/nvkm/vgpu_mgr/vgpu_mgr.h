/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_VGPU_MGR_H__
#define __NVKM_VGPU_MGR_H__

#define NVIDIA_MAX_VGPUS 2

struct nvkm_vgpu_mgr {
	bool enabled;
	struct nvkm_device *nvkm_dev;
};

bool nvkm_vgpu_mgr_is_supported(struct nvkm_device *device);
bool nvkm_vgpu_mgr_is_enabled(struct nvkm_device *device);
int nvkm_vgpu_mgr_init(struct nvkm_device *device);
void nvkm_vgpu_mgr_fini(struct nvkm_device *device);

#endif
