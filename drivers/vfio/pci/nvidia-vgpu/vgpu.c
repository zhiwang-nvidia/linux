/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#include "vgpu_mgr.h"

static void unregister_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	mutex_lock(&vgpu_mgr->vgpu_id_lock);

	vgpu_mgr->vgpus[vgpu->info.id] = NULL;
	atomic_dec(&vgpu_mgr->num_vgpus);

	mutex_unlock(&vgpu_mgr->vgpu_id_lock);
}

static int register_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	mutex_lock(&vgpu_mgr->vgpu_id_lock);

	if (vgpu_mgr->vgpus[vgpu->info.id]) {
		mutex_unlock(&vgpu_mgr->vgpu_id_lock);
		return -EBUSY;
	}
	vgpu_mgr->vgpus[vgpu->info.id] = vgpu;
	atomic_inc(&vgpu_mgr->num_vgpus);

	mutex_unlock(&vgpu_mgr->vgpu_id_lock);
	return 0;
}

/**
 * nvidia_vgpu_mgr_destroy_vgpu - destroy a vGPU instance
 * @vgpu: the vGPU instance going to be destroyed.
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_destroy_vgpu(struct nvidia_vgpu *vgpu)
{
	if (!atomic_cmpxchg(&vgpu->status, 1, 0))
		return -ENODEV;

	unregister_vgpu(vgpu);
	return 0;
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_destroy_vgpu);

/**
 * nvidia_vgpu_mgr_create_vgpu - create a vGPU instance
 * @vgpu: the vGPU instance going to be created.
 * @vgpu_type: the vGPU type of the vGPU instance.
 *
 * The caller must initialize vgpu->vgpu_mgr, gpu->info, vgpu->pdev.
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu, u8 *vgpu_type)
{
	int ret;

	if (WARN_ON(vgpu->info.id >= NVIDIA_MAX_VGPUS))
		return -EINVAL;

	if (WARN_ON(!vgpu->vgpu_mgr || !vgpu->info.gfid || !vgpu->info.dbdf))
		return -EINVAL;

	mutex_init(&vgpu->lock);
	vgpu->vgpu_type = vgpu_type;

	ret = register_vgpu(vgpu);
	if (ret)
		return ret;

	atomic_set(&vgpu->status, 1);

	return 0;
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_create_vgpu);
