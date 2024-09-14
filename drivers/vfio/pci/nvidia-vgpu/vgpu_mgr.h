/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_MGR_H__
#define __NVIDIA_VGPU_MGR_H__

#include "pf.h"

/**
 * struct nvidia_vgpu_info - vGPU information
 *
 * @id: vGPU ID
 * @gfid: VF function ID
 * @dbdf: VF BDF
 */
struct nvidia_vgpu_info {
	int id;
	u32 gfid;
	u32 dbdf;
};

/**
 * struct nvidia_vgpu - per-vGPU state
 *
 * @lock: per-vGPU lock
 * @pdev: PCI device
 * @status: vGPU status
 * @vgpu_list: list node to the vGPU list
 * @info: vGPU info
 * @vgpu_mgr: pointer to vGPU manager
 */
struct nvidia_vgpu {
	/* Per-vGPU lock */
	struct mutex lock;
	struct pci_dev *pdev;
	atomic_t status;
	struct list_head vgpu_list;

	struct nvidia_vgpu_info info;
	struct nvidia_vgpu_mgr *vgpu_mgr;
};

/**
 * struct nvidia_vgpu_mgr - the vGPU manager
 *
 * @refcount: the reference count
 * @handle: the driver handle
 * @total_avail_chids: total available channel IDs
 * @total_fbmem_size: total FB memory size
 * @vgpu_list_lock: lock to protect vGPU list
 * @vgpu_list_head: list head of vGPU list
 * @num_vgpus: number of vGPUs in the vGPU list
 * @gsp_client: the GSP client
 */
struct nvidia_vgpu_mgr {
	struct kref refcount;
	struct nvidia_vgpu_mgr_handle handle;

	/* core driver configurations */
	u32 total_avail_chids;
	u64 total_fbmem_size;

	/* lock for vGPU list */
	struct mutex vgpu_list_lock;
	struct list_head vgpu_list_head;
	atomic_t num_vgpus;

	struct nvidia_vgpu_gsp_client gsp_client;
};

#define nvidia_vgpu_mgr_for_each_vgpu(vgpu, vgpu_mgr) \
	list_for_each_entry((vgpu), &(vgpu_mgr)->vgpu_list_head, vgpu_list)

int nvidia_vgpu_mgr_setup(struct pci_dev *dev, int (*init_vfio_fn)(void *priv, void *data),
			  void *init_vfio_fn_data);
void nvidia_vgpu_mgr_release(struct nvidia_vgpu_mgr *vgpu_mgr);

int nvidia_vgpu_mgr_destroy_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu);

#endif
