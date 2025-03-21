// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/log2.h>

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

static void clean_chids(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_chid *chid = &vgpu->chid;

	vgpu_debug(vgpu, "free guest channel offset %d size %d\n", chid->chid_offset,
		   chid->num_chid);

	if (vgpu_mgr->use_chid_alloc_bitmap)
		bitmap_clear(vgpu_mgr->chid_alloc_bitmap, chid->chid_offset, chid->num_chid);
	else
		nvidia_vgpu_mgr_free_chids(vgpu_mgr, chid->chid_offset, chid->num_chid);
}

static inline u32 prev_pow2(const u32 x)
{
	return x ? 1U << ilog2(x) : 0;
}

static void get_alloc_chids_num(struct nvidia_vgpu *vgpu, u32 *size)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_info *info = &vgpu->info;
	struct nvidia_vgpu_type *type = info->vgpu_type;
	u32 v;

	/* Calculate with total reserved CHIDs for vGPUs. */
	v = (vgpu_mgr->total_avail_chids) / type->max_instance;
	*size = prev_pow2(v);
}

static int setup_chids(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_chid *chid = &vgpu->chid;
	u32 size, offset;
	int ret;

	get_alloc_chids_num(vgpu, &size);

	if (vgpu_mgr->use_chid_alloc_bitmap) {
		offset = bitmap_find_next_zero_area(vgpu_mgr->chid_alloc_bitmap,
						    vgpu_mgr->total_avail_chids, 0, size, 0);

		if (offset + size > vgpu_mgr->total_avail_chids)
			return -ENOSPC;

		bitmap_set(vgpu_mgr->chid_alloc_bitmap, offset, size);
	} else {
		ret = nvidia_vgpu_mgr_alloc_chids(vgpu_mgr, &offset, size);
		if (ret)
			return ret;
	}

	chid->chid_offset = offset;
	chid->num_chid = size;
	chid->num_plugin_channels = 1;

	vgpu_debug(vgpu, "alloc guest channel offset %u size %u\n", chid->chid_offset,
		   chid->num_chid);
	return 0;
}

static void clean_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	vgpu_debug(vgpu, "free guest FB memory, offset 0x%llx size 0x%llx\n",
		   vgpu->fbmem_heap->addr, vgpu->fbmem_heap->size);

	nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, vgpu->fbmem_heap);
	vgpu->fbmem_heap = NULL;
}

static int get_alloc_fbmem_size(struct nvidia_vgpu *vgpu, u64 *size)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_info *info = &vgpu->info;
	struct nvidia_vgpu_type *type = info->vgpu_type;
	u64 fb_length;

	if (!vgpu_mgr->ecc_enabled) {
		*size = type->fb_length;
		return 0;
	}

	if (!info->vgpu_type->ecc_supported) {
		vgpu_error(vgpu, "ECC is enabled. vGPU type %s doesn't support ECC!\n",
			   type->vgpu_type_name);
		return -ENODEV;
	}

	/* Re-calculate the FB memory length when ECC is enabled. */
	fb_length = ALIGN(vgpu_mgr->total_fbmem_size, vgpu_mgr->vmmu_segment_size);
	fb_length = fb_length / type->max_instance - type->fb_reservation - type->gsp_heap_size;
	fb_length = min(type->fb_length, fb_length);
	fb_length = ALIGN_DOWN(fb_length, vgpu_mgr->vmmu_segment_size);

	*size = fb_length;
	return 0;
}

static int setup_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_alloc_fbmem_info info = {0};
	struct nvidia_vgpu_mem *mem;
	int ret;

	ret = get_alloc_fbmem_size(vgpu, &info.size);
	if (ret)
		return ret;

	info.align = vgpu_mgr->vmmu_segment_size;

	vgpu_debug(vgpu, "alloc guest FB memory, size 0x%llx\n", info.size);

	mem = nvidia_vgpu_mgr_alloc_fbmem(vgpu_mgr, &info);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	vgpu_debug(vgpu, "guest FB memory offset 0x%llx size 0x%llx\n", mem->addr, mem->size);

	vgpu->fbmem_heap = mem;
	return 0;
}

static void clean_mgmt_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;

	nvidia_vgpu_mgr_bar1_unmap_mem(vgpu_mgr, mgmt->heap_mem);

	vgpu_debug(vgpu, "free mgmt heap, offset 0x%llx size 0x%llx\n", mgmt->heap_mem->addr,
		   mgmt->heap_mem->size);

	nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, mgmt->heap_mem);
	mgmt->init_task_log_vaddr = mgmt->vgpu_task_log_vaddr = NULL;
	mgmt->ctrl_vaddr = mgmt->kernel_log_vaddr = NULL;
	mgmt->heap_mem = NULL;
}

static int setup_mgmt_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;
	struct nvidia_vgpu_info *info = &vgpu->info;
	struct nvidia_vgpu_type *vgpu_type = info->vgpu_type;
	struct nvidia_vgpu_alloc_fbmem_info alloc_info = {0};
	struct nvidia_vgpu_map_mem_info map_info = {0};
	struct nvidia_vgpu_mem *mem;
	int ret;

	alloc_info.size = vgpu_type->gsp_heap_size;

	vgpu_debug(vgpu, "alloc mgmt heap, size 0x%llx\n", alloc_info.size);

	mem = nvidia_vgpu_mgr_alloc_fbmem(vgpu_mgr, &alloc_info);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	vgpu_debug(vgpu, "mgmt heap offset 0x%llx size 0x%llx\n", mem->addr, mem->size);

	map_info.map_size = vgpu_mgr->comm_buff_size;

	ret = nvidia_vgpu_mgr_bar1_map_mem(vgpu_mgr, mem, &map_info);
	if (ret) {
		nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, mem);
		return ret;
	}

	vgpu_debug(vgpu, "mgmt heap mapped\n");

	mgmt->ctrl_vaddr = mem->bar1_vaddr;
	mgmt->init_task_log_vaddr = mgmt->ctrl_vaddr +
				    vgpu_mgr->init_task_log_offset;
	mgmt->vgpu_task_log_vaddr = mgmt->init_task_log_vaddr +
				    vgpu_mgr->init_task_log_size;
	mgmt->kernel_log_vaddr = mgmt->vgpu_task_log_vaddr +
				 vgpu_mgr->vgpu_task_log_size;
	mgmt->heap_mem = mem;
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

	clean_mgmt_heap(vgpu);
	clean_fbmem_heap(vgpu);
	clean_chids(vgpu);
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

	ret = setup_chids(vgpu);
	if (ret)
		goto err_setup_chids;

	ret = setup_fbmem_heap(vgpu);
	if (ret)
		goto err_setup_fbmem_heap;

	ret = setup_mgmt_heap(vgpu);
	if (ret)
		goto err_setup_mgmt_heap;

	atomic_set(&vgpu->status, 1);

	vgpu_debug(vgpu, "created\n");

	return 0;

err_setup_mgmt_heap:
	clean_fbmem_heap(vgpu);
err_setup_fbmem_heap:
	clean_chids(vgpu);
err_setup_chids:
	unregister_vgpu(vgpu);

	return ret;
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_create_vgpu);
