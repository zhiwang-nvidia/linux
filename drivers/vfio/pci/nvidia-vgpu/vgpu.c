// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#include <linux/log2.h>

#include "debug.h"
#include "vgpu_mgr.h"

#include <nvrm/bootload.h>
#include <nvrm/vgpu.h>

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

static void clean_ce_channel(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_ce_channel *channel = &vgpu->ce_channel;

	nvidia_vgpu_event_unregister_listener(&vgpu_mgr->pf_channel_event_chain,
					      &channel->listener);

	nvidia_vgpu_mgr_channel_unmap_mem(vgpu_mgr, channel->sema_mem);
	nvidia_vgpu_mgr_bar1_unmap_mem(vgpu_mgr, channel->sema_mem);
	nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, channel->sema_mem);
	nvidia_vgpu_mgr_free_ce_channel(vgpu_mgr, channel->chan);
	channel->chan = NULL;
	channel->sema_mem = NULL;
}

static int handle_channel_events(struct nvidia_vgpu_event_listener *self, unsigned int event,
				 void *data)
{
	struct nvidia_vgpu_ce_channel *channel = container_of(self, typeof(*channel), listener);
	struct nvidia_vgpu *vgpu = container_of(channel, typeof(*vgpu), ce_channel);

	if (data != channel->chan)
		return 0;

	switch (event) {
	case NVIDIA_VGPU_PF_CHANNEL_EVENT_FIFO_NONSTALL:
		vgpu_debug(vgpu, "handle channel event fifo nonstall\n");

		wake_up(&channel->wq);
		break;
	}
	return 0;
}

static int setup_ce_channel(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_ce_channel *channel = &vgpu->ce_channel;
	struct nvidia_vgpu_chid *chid = &vgpu->chid;
	struct nvidia_vgpu_alloc_fbmem_info alloc_info = {0};
	struct nvidia_vgpu_map_mem_info map_info = {0};
	struct nvidia_vgpu_chan *chan;
	struct nvidia_vgpu_mem *mem;
	int ret;

	chan = nvidia_vgpu_mgr_alloc_ce_channel(vgpu_mgr, chid->chid_offset + chid->num_chid - 1);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	/* Allocate a page for semaphore */
	alloc_info.size = SZ_4K;

	mem = nvidia_vgpu_mgr_alloc_fbmem(vgpu_mgr, &alloc_info);
	if (IS_ERR(mem))
		goto err_alloc_fbmem;

	map_info.map_size = SZ_4K;

	ret = nvidia_vgpu_mgr_channel_map_mem(vgpu_mgr, chan, mem, &map_info);
	if (ret)
		goto err_chan_map_mem;

	ret = nvidia_vgpu_mgr_bar1_map_mem(vgpu_mgr, mem, &map_info);
	if (ret)
		goto err_bar1_map_mem;

	channel->chan = chan;
	channel->sema_mem = mem;

	init_waitqueue_head(&channel->wq);

	INIT_LIST_HEAD(&channel->listener.list);
	channel->listener.func = handle_channel_events;

	nvidia_vgpu_event_register_listener(&vgpu_mgr->pf_channel_event_chain,
					    &channel->listener);

	return 0;

err_bar1_map_mem:
	nvidia_vgpu_mgr_channel_unmap_mem(vgpu_mgr, mem);
err_chan_map_mem:
	nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, mem);
err_alloc_fbmem:
	nvidia_vgpu_mgr_free_ce_channel(vgpu_mgr, chan);
	return ret;
}

static bool ce_workload_complete(struct nvidia_vgpu_ce_channel *channel)
{
	return !!READ_ONCE(*(u32 *)(channel->sema_mem->bar1_vaddr));
}

#define VGPU_SCRUBBER_LINE_LENGTH_MAX 0x80000000
#define FBMEM_SCRUB_TIMEOUT_MS (4000)

static int scrub_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_ce_channel *channel;
	struct nvidia_vgpu_chan *chan;
	struct nvidia_vgpu_mem *mem = vgpu->fbmem_heap;
	struct nvidia_vgpu_map_mem_info map_info = {0};
	u64 line_length = mem->size;
	u32 line_count = 1;
	int ret;
	int i;

	if (WARN_ON(!vgpu_mgr->use_ce_scrub_fbmem))
		return 0;

	ret = setup_ce_channel(vgpu);
	if (ret)
		return ret;

	channel = &vgpu->ce_channel;
	chan = channel->chan;

	map_info.compressible_disable_plc = true;
	map_info.huge_page = true;
	map_info.map_size = mem->size;

	ret = nvidia_vgpu_mgr_channel_map_mem(vgpu_mgr, chan, mem, &map_info);
	if (ret)
		goto err_chan_map_mem;

	vgpu_debug(vgpu, "guest FB memory chan vma 0x%llx\n", mem->chan_vma_addr);

	while (line_length > VGPU_SCRUBBER_LINE_LENGTH_MAX) {
		line_count = line_count << 1;
		line_length = line_length >> 1;
	}

	*(u32 *)(channel->sema_mem->bar1_vaddr) = 0;

	vgpu_debug(vgpu, "semaphore seqno before scrubbing 0x%x\n",
		   *(u32 *)(channel->sema_mem->bar1_vaddr));

	nvidia_vgpu_mgr_begin_pushbuf(vgpu_mgr, chan, 150);

#define EMIT_DWORD(x) \
	nvidia_vgpu_mgr_emit_pushbuf(vgpu_mgr, chan, x)

	for (i = 0; i < 128; i += 4)
		EMIT_DWORD(0x0);

	EMIT_DWORD(0x20010000);
	EMIT_DWORD(chan->ce_object_handle);
	EMIT_DWORD(0x200181c2);
	EMIT_DWORD(0x30004);

	EMIT_DWORD(0x200181c0);
	EMIT_DWORD(0x0);

	EMIT_DWORD(0x20048104);
	EMIT_DWORD(lower_32_bits(line_length));
	EMIT_DWORD(lower_32_bits(line_length));
	EMIT_DWORD(lower_32_bits(line_length >> 2));
	EMIT_DWORD(line_count);

	EMIT_DWORD(0x20028102);
	EMIT_DWORD(upper_32_bits(vgpu->fbmem_heap->chan_vma_addr));
	EMIT_DWORD(lower_32_bits(vgpu->fbmem_heap->chan_vma_addr));

	EMIT_DWORD(0x200180c0);
	EMIT_DWORD(0x785);

	EMIT_DWORD(0x20038090);
	EMIT_DWORD(upper_32_bits(vgpu->ce_channel.sema_mem->chan_vma_addr));
	EMIT_DWORD(lower_32_bits(vgpu->ce_channel.sema_mem->chan_vma_addr));
	EMIT_DWORD(0xdeadbeef);

	EMIT_DWORD(0x200180c0);
	EMIT_DWORD(0x5cc);

#undef EMIT_DWORD

	nvidia_vgpu_mgr_submit_pushbuf(vgpu_mgr, chan);

	if (!wait_event_timeout(channel->wq, ce_workload_complete(channel),
				msecs_to_jiffies(FBMEM_SCRUB_TIMEOUT_MS))) {
		vgpu_debug(vgpu, "fail to wait for CE workload complete\n");

		ret = -ETIMEDOUT;
		goto err_pushbuf;
	}

	vgpu_debug(vgpu, "semaphore seqno after scrubbing 0x%x\n",
		   *(u32 *)(channel->sema_mem->bar1_vaddr));

	nvidia_vgpu_mgr_channel_unmap_mem(vgpu_mgr, mem);
	clean_ce_channel(vgpu);

	return 0;

err_pushbuf:
	nvidia_vgpu_mgr_channel_unmap_mem(vgpu_mgr, mem);
err_chan_map_mem:
	clean_ce_channel(vgpu);
	return ret;
}

static void clean_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	vgpu_debug(vgpu, "free guest FB memory, offset 0x%llx size 0x%llx\n",
		   vgpu->fbmem_heap->addr, vgpu->fbmem_heap->size);

	if (vgpu_mgr->use_ce_scrub_fbmem)
		WARN_ON(scrub_fbmem_heap(vgpu));
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

	return vgpu_mgr->use_ce_scrub_fbmem ? scrub_fbmem_heap(vgpu) : 0;
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

static int shutdown_vgpu_plugin_task(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS *ctrl;

	ctrl = nvidia_vgpu_mgr_rm_ctrl_get(vgpu_mgr, &vgpu->gsp_client,
			NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK,
			sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->gfid = vgpu->info.gfid;

	return nvidia_vgpu_mgr_rm_ctrl_wr(vgpu_mgr, &vgpu->gsp_client,
					  ctrl);
}

static int cleanup_vgpu_plugin_task(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS *ctrl;

	ctrl = nvidia_vgpu_mgr_rm_ctrl_get(vgpu_mgr, &vgpu->gsp_client,
			NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP,
			sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->gfid = vgpu->info.gfid;

	return nvidia_vgpu_mgr_rm_ctrl_wr(vgpu_mgr, &vgpu->gsp_client,
					  ctrl);
}

static int bootload_vgpu_plugin_task(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;
	NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS *ctrl;
	int ret, i;

	vgpu_debug(vgpu, "bootload\n");

	ctrl = nvidia_vgpu_mgr_rm_ctrl_get(vgpu_mgr, &vgpu->gsp_client,
			NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK,
			sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->dbdf = vgpu->info.dbdf;
	ctrl->gfid = vgpu->info.gfid;
	ctrl->vmPid = vgpu->info.vm_pid;
	ctrl->swizzId = 0;
	ctrl->numChannels = vgpu->chid.num_chid;
	ctrl->numPluginChannels = vgpu->chid.num_plugin_channels;

	for_each_set_bit(i, vgpu_mgr->engine_bitmap, NV2080_GPU_MAX_ENGINES)
		ctrl->chidOffset[i] = vgpu->chid.chid_offset;

	ctrl->bDisableDefaultSmcExecPartRestore = false;
	ctrl->numGuestFbSegments = 1;
	ctrl->guestFbPhysAddrList[0] = vgpu->fbmem_heap->addr;
	ctrl->guestFbLengthList[0] = vgpu->fbmem_heap->size;
	ctrl->pluginHeapMemoryPhysAddr = mgmt->heap_mem->addr;
	ctrl->pluginHeapMemoryLength = mgmt->heap_mem->size;
	ctrl->ctrlBuffOffset = 0;
	ctrl->initTaskLogBuffOffset = mgmt->heap_mem->addr +
				      vgpu_mgr->init_task_log_offset;
	ctrl->initTaskLogBuffSize = vgpu_mgr->init_task_log_size;
	ctrl->vgpuTaskLogBuffOffset = ctrl->initTaskLogBuffOffset +
				      ctrl->initTaskLogBuffSize;
	ctrl->vgpuTaskLogBuffSize = vgpu_mgr->vgpu_task_log_size;
	ctrl->kernelLogBuffOffset = ctrl->vgpuTaskLogBuffOffset +
				      ctrl->vgpuTaskLogBuffSize;
	ctrl->kernelLogBuffSize = vgpu_mgr->kernel_log_size;

	ctrl->bDeviceProfilingEnabled = false;

	ret = nvidia_vgpu_mgr_rm_ctrl_wr(vgpu_mgr, &vgpu->gsp_client,
					 ctrl);
	if (ret)
		return ret;

	vgpu_debug(vgpu, "bootloading\n");
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
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	if (!atomic_cmpxchg(&vgpu->status, 1, 0))
		return -ENODEV;

	nvidia_vgpu_clean_rpc(vgpu);
	WARN_ON(shutdown_vgpu_plugin_task(vgpu));
	WARN_ON(cleanup_vgpu_plugin_task(vgpu));
	nvidia_vgpu_mgr_free_gsp_client(vgpu_mgr, &vgpu->gsp_client);
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
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_info *info = &vgpu->info;
	int ret;

	if (WARN_ON(!info->gfid || !info->dbdf || !info->vgpu_type || !info->vm_pid))
		return -EINVAL;

	if (WARN_ON(!vgpu->vgpu_mgr || !vgpu->pdev))
		return -EINVAL;

	mutex_init(&vgpu->lock);
	INIT_LIST_HEAD(&vgpu->vgpu_list);

	vgpu->info = *info;

	vgpu_debug(vgpu, "create vgpu %s on vgpu_mgr %px vm pid %u\n",
		   info->vgpu_type->vgpu_type_name, vgpu->vgpu_mgr, info->vm_pid);

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

	ret = nvidia_vgpu_mgr_alloc_gsp_client(vgpu_mgr,
					       &vgpu->gsp_client);
	if (ret)
		goto err_alloc_gsp_client;

	ret = bootload_vgpu_plugin_task(vgpu);
	if (ret)
		goto err_bootload_vgpu_plugin_task;

	ret = nvidia_vgpu_setup_rpc(vgpu);
	if (ret)
		goto err_setup_rpc;

	atomic_set(&vgpu->status, 1);

	vgpu_debug(vgpu, "created\n");

	return 0;

err_setup_rpc:
	shutdown_vgpu_plugin_task(vgpu);
	cleanup_vgpu_plugin_task(vgpu);
err_bootload_vgpu_plugin_task:
	nvidia_vgpu_mgr_free_gsp_client(vgpu_mgr, &vgpu->gsp_client);
err_alloc_gsp_client:
	clean_mgmt_heap(vgpu);
err_setup_mgmt_heap:
	clean_fbmem_heap(vgpu);
err_setup_fbmem_heap:
	clean_chids(vgpu);
err_setup_chids:
	unregister_vgpu(vgpu);

	return ret;
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_create_vgpu);

/**
 * nvidia_vgpu_mgr_reset_vgpu - reset a vGPU instance
 * @vgpu: the vGPU instance going to be reset.
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_reset_vgpu(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	int ret;

	ret = nvidia_vgpu_rpc_call(vgpu, NV_VGPU_CPU_RPC_MSG_RESET, NULL, 0);
	if (ret) {
		vgpu_error(vgpu, "fail to reset vgpu ret %d\n", ret);
		return ret;
	}

	if (vgpu_mgr->use_ce_scrub_fbmem) {
		ret = scrub_fbmem_heap(vgpu);
		if (ret) {
			vgpu_error(vgpu, "fail to scrub the fbmem %d\n", ret);
			return ret;
		}
	}

	vgpu_debug(vgpu, "reset done\n");
	return 0;
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_reset_vgpu);

static int update_bme_state(struct nvidia_vgpu *vgpu, bool enable)
{
	NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE params = {0};

	params.enable = enable;

	return nvidia_vgpu_rpc_call(vgpu, NV_VGPU_CPU_RPC_MSG_UPDATE_BME_STATE,
				    &params, sizeof(params));
}

/**
 * nvidia_vgpu_set_bme - handle BME sequence
 * @vgpu: the vGPU instance
 * @enable: BME enable/disable
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_set_bme(struct nvidia_vgpu *vgpu, bool enable)
{
	vgpu_debug(vgpu, "set bme, enable %d\n", enable);

	return update_bme_state(vgpu, enable);
}
EXPORT_SYMBOL_GPL(nvidia_vgpu_mgr_set_bme);
