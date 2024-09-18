/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#include <linux/kernel.h>

#include <nvrm/nvtypes.h>
#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrla081.h>
#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080vgpumgrinternal.h>
#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrl0000/ctrl0000system.h>
#include <nvrm/common/sdk/vmioplugin/inc/vmioplugin.h>
#include <nvrm/common/sdk/nvidia/inc/dev_vgpu_gsp.h>

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

static void clean_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	nvidia_vgpu_mgr_free_fbmem_heap(vgpu_mgr, vgpu->fbmem_heap);
	vgpu->fbmem_heap = NULL;
}

static int setup_fbmem_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	NVA081_CTRL_VGPU_INFO *info =
		(NVA081_CTRL_VGPU_INFO *)vgpu->vgpu_type;
	struct nvidia_vgpu_mem *mem;

	mem = nvidia_vgpu_mgr_alloc_fbmem_heap(vgpu_mgr, info->fbLength);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	vgpu->fbmem_heap = mem;
	return 0;
}

static void clean_chids(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_chid *chid = &vgpu->chid;

	nvidia_vgpu_mgr_free_chids(vgpu_mgr, chid->chid_offset, chid->num_chid);
}

static int setup_chids(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_chid *chid = &vgpu->chid;
	int ret;

	ret = nvidia_vgpu_mgr_alloc_chids(vgpu_mgr, 512);
	if (ret < 0)
		return ret;

	chid->chid_offset = ret;
	chid->num_chid = 512;
	chid->num_plugin_channels = 0;

	return 0;
}

static inline u64 init_task_log_buff_offset(void)
{
	return (3 * SZ_4K) + SZ_2M + SZ_4K;
}

static inline u64 init_task_log_buff_size(void)
{
	return SZ_128K;
}

static inline u64 vgpu_task_log_buff_size(void)
{
	return SZ_128K;
}

static void clean_mgmt_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;

	nvidia_vgpu_mgr_bar1_unmap_mem(vgpu_mgr, mgmt->heap_mem);
	nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, mgmt->heap_mem);
	mgmt->init_task_log_vaddr = mgmt->vgpu_task_log_vaddr = NULL;
	mgmt->heap_mem = NULL;
}

static int setup_mgmt_heap(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;
	NVA081_CTRL_VGPU_INFO *info =
		(NVA081_CTRL_VGPU_INFO *)vgpu->vgpu_type;
	struct nvidia_vgpu_mem *mem;
	int ret;

	mem = nvidia_vgpu_mgr_alloc_fbmem(vgpu_mgr, info->gspHeapSize);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	ret = nvidia_vgpu_mgr_bar1_map_mem(vgpu_mgr, mem);
	if (ret) {
		nvidia_vgpu_mgr_free_fbmem(vgpu_mgr, mem);
		return ret;
	}

	mgmt->ctrl_vaddr = mem->bar1_vaddr;
	mgmt->init_task_log_vaddr = mgmt->ctrl_vaddr +
				    init_task_log_buff_offset();
	mgmt->vgpu_task_log_vaddr = mgmt->init_task_log_vaddr +
				    init_task_log_buff_size();
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
		return PTR_ERR(ctrl);;

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
	DECLARE_BITMAP(engine_bitmap, NV2080_GPU_MAX_ENGINES);
	int ret, i;

	ctrl = nvidia_vgpu_mgr_rm_ctrl_get(vgpu_mgr, &vgpu->gsp_client,
			NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK,
			sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->dbdf = vgpu->info.dbdf;
	ctrl->gfid = vgpu->info.gfid;
	ctrl->vmPid = 0;
	ctrl->swizzId = 0;
	ctrl->numChannels = vgpu->chid.num_chid;
	ctrl->numPluginChannels = 0;

	bitmap_clear(engine_bitmap, 0, NV2080_GPU_MAX_ENGINES);

	/* FIXME: nvkm seems not correctly record engines. two engines are missing. */
	nvidia_vgpu_mgr_get_engine_bitmap(vgpu_mgr, engine_bitmap);

	for_each_set_bit(i, engine_bitmap, NV2080_GPU_MAX_ENGINES)
		ctrl->chidOffset[i] = vgpu->chid.chid_offset;

	ctrl->bDisableDefaultSmcExecPartRestore = false;
	ctrl->numGuestFbSegments = 1;
	ctrl->guestFbPhysAddrList[0] = vgpu->fbmem_heap->addr;
	ctrl->guestFbLengthList[0] = vgpu->fbmem_heap->size;
	ctrl->pluginHeapMemoryPhysAddr = mgmt->heap_mem->addr;
	ctrl->pluginHeapMemoryLength = mgmt->heap_mem->size;
	ctrl->ctrlBuffOffset = 0;
	ctrl->initTaskLogBuffOffset = mgmt->heap_mem->addr +
				      init_task_log_buff_offset();
	ctrl->initTaskLogBuffSize = init_task_log_buff_size();
	ctrl->vgpuTaskLogBuffOffset = ctrl->initTaskLogBuffOffset +
				      ctrl->initTaskLogBuffSize;
	ctrl->vgpuTaskLogBuffSize = vgpu_task_log_buff_size();
	ctrl->bDeviceProfilingEnabled = false;

	ret = nvidia_vgpu_mgr_rm_ctrl_wr(vgpu_mgr, &vgpu->gsp_client,
					 ctrl);
	if (ret)
		return ret;
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
	clean_chids(vgpu);
	clean_fbmem_heap(vgpu);
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
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
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

	ret = setup_fbmem_heap(vgpu);
	if (ret)
		goto err_setup_fbmem_heap;

	ret = setup_chids(vgpu);
	if (ret)
		goto err_setup_chids;

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

	return 0;

err_setup_rpc:
	shutdown_vgpu_plugin_task(vgpu);
	cleanup_vgpu_plugin_task(vgpu);
err_bootload_vgpu_plugin_task:
	nvidia_vgpu_mgr_free_gsp_client(vgpu_mgr, &vgpu->gsp_client);
err_alloc_gsp_client:
	clean_mgmt_heap(vgpu);
err_setup_mgmt_heap:
	clean_chids(vgpu);
err_setup_chids:
	clean_fbmem_heap(vgpu);
err_setup_fbmem_heap:
	unregister_vgpu(vgpu);

	return ret;
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_create_vgpu);

static int update_bme_state(struct nvidia_vgpu *vgpu)
{
	NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE params = {0};

	params.enable = true;

	return nvidia_vgpu_rpc_call(vgpu, NV_VGPU_CPU_RPC_MSG_UPDATE_BME_STATE,
				    &params, sizeof(params));
}

/**
 * nvidia_vgpu_enable_bme - handle BME sequence
 * @vf: the vGPU instance
 *
 * Returns: 0 on success, others on failure.
 */
int nvidia_vgpu_mgr_enable_bme(struct nvidia_vgpu *vgpu)
{
	return update_bme_state(vgpu);
}
EXPORT_SYMBOL(nvidia_vgpu_mgr_enable_bme);
