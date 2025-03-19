// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include "debug.h"
#include "vgpu_mgr.h"

static void unregister_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	mutex_lock(&vgpu_mgr->vgpu_list_lock);

	list_del(&vgpu->vgpu_list);
	atomic_dec(&vgpu_mgr->num_vgpus);

	mutex_unlock(&vgpu_mgr->vgpu_list_lock);

	vgpu_debug(vgpu, "unregistered\n");
}

static int register_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu *p;

	mutex_lock(&vgpu_mgr->vgpu_list_lock);

	nvidia_vgpu_mgr_for_each_vgpu(p, vgpu_mgr) {
		if (WARN_ON(p->info.id == vgpu->info.id)) {
			mutex_unlock(&vgpu_mgr->vgpu_list_lock);
			return -EBUSY;
		}
	}

	list_add_tail(&vgpu->vgpu_list, &vgpu_mgr->vgpu_list_head);
	atomic_inc(&vgpu_mgr->num_vgpus);

	mutex_unlock(&vgpu_mgr->vgpu_list_lock);

	vgpu_debug(vgpu, "registered\n");
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

	vgpu_debug(vgpu, "destroyed\n");

	return 0;
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_destroy_vgpu);

/**
 * nvidia_vgpu_mgr_create_vgpu - create a vGPU instance
 * @vgpu: the vGPU instance going to be created.
 *
 * The caller must initialize vgpu->vgpu_mgr, vgpu->pdev and vgpu->info.
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_info *info = &vgpu->info;
	int ret;

	if (WARN_ON(!info->gfid || !info->dbdf || !info->vgpu_type))
		return -EINVAL;

	if (WARN_ON(!vgpu->vgpu_mgr || !vgpu->pdev))
		return -EINVAL;

	mutex_init(&vgpu->lock);
	INIT_LIST_HEAD(&vgpu->vgpu_list);

	vgpu->info = *info;

	vgpu_debug(vgpu, "create vgpu %s on vgpu_mgr %px\n",
		   info->vgpu_type->vgpu_type_name, vgpu->vgpu_mgr);

	ret = register_vgpu(vgpu);
	if (ret)
		return ret;

	atomic_set(&vgpu->status, 1);

	vgpu_debug(vgpu, "created\n");

	return 0;
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_create_vgpu);
