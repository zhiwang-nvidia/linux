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

struct nvidia_vgpu_chid {
	int chid_offset;
	int num_chid;
	int num_plugin_channels;
};

struct nvidia_vgpu_mgmt {
	struct nvidia_vgpu_mem *heap_mem;
	void __iomem *ctrl_vaddr;
	void __iomem *init_task_log_vaddr;
	void __iomem *vgpu_task_log_vaddr;
};

struct nvidia_vgpu_rpc {
	struct mutex lock;
	u32 msg_seq_num;
	void __iomem *ctrl_buf;
	void __iomem *resp_buf;
	void __iomem *msg_buf;
	void __iomem *migration_buf;
	void __iomem *error_buf;
};

struct nvidia_vgpu {
	struct mutex lock;
	atomic_t status;
	struct pci_dev *pdev;

	u8 *vgpu_type;
	struct nvidia_vgpu_info info;
	struct nvidia_vgpu_mgr *vgpu_mgr;
	struct nvidia_vgpu_gsp_client gsp_client;

	struct nvidia_vgpu_mem *fbmem_heap;
	struct nvidia_vgpu_chid chid;
	struct nvidia_vgpu_mgmt mgmt;
	struct nvidia_vgpu_rpc rpc;
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

	struct pci_dev *pdev;
	void __iomem *bar0_vaddr;
};

struct nvidia_vgpu_mgr *nvidia_vgpu_mgr_get(struct pci_dev *dev);
void nvidia_vgpu_mgr_put(struct nvidia_vgpu_mgr *vgpu_mgr);

int nvidia_vgpu_mgr_destroy_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu, u8 *vgpu_type);

int nvidia_vgpu_mgr_init_vgpu_types(struct nvidia_vgpu_mgr *vgpu_mgr);

int nvidia_vgpu_rpc_call(struct nvidia_vgpu *vgpu, u32 msg_type,
			 void *data, u64 size);
void nvidia_vgpu_clean_rpc(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_setup_rpc(struct nvidia_vgpu *vgpu);

int nvidia_vgpu_mgr_enable_bme(struct nvidia_vgpu *vgpu);

#endif
