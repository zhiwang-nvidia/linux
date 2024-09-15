/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_MGR_H__
#define __NVIDIA_VGPU_MGR_H__

#include "nvkm.h"

#define NVIDIA_MAX_VGPUS 2

struct nvidia_vgpu_info {
	int id;
	u32 gfid;
	u32 dbdf;
};

struct nvidia_vgpu {
	struct mutex lock;
	atomic_t status;
	struct pci_dev *pdev;

	u8 *vgpu_type;
	struct nvidia_vgpu_info info;
	struct nvidia_vgpu_mgr *vgpu_mgr;
};

struct nvidia_vgpu_mgr {
	struct kref refcount;
	struct nvidia_vgpu_mgr_handle handle;

	struct mutex vgpu_id_lock;
	struct nvidia_vgpu *vgpus[NVIDIA_MAX_VGPUS];
	atomic_t num_vgpus;

	u8 **vgpu_types;
	u32 num_vgpu_types;

	struct nvidia_vgpu_gsp_client gsp_client;
};

struct nvidia_vgpu_mgr *nvidia_vgpu_mgr_get(struct pci_dev *dev);
void nvidia_vgpu_mgr_put(struct nvidia_vgpu_mgr *vgpu_mgr);

int nvidia_vgpu_mgr_destroy_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu, u8 *vgpu_type);

int nvidia_vgpu_mgr_init_vgpu_types(struct nvidia_vgpu_mgr *vgpu_mgr);

#endif
