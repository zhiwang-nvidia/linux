/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_MGR_H__
#define __NVIDIA_VGPU_MGR_H__

#include "pf.h"

#define NVIDIA_VGPU_TYPE_NAME_MAX 32

struct nvidia_vgpu_type {
	u32 vgpu_type;
	char vgpu_type_name[NVIDIA_VGPU_TYPE_NAME_MAX];
	u64 vdev_id;
	u64 pdev_id;
	u64 fb_length;
	u64 gsp_heap_size;
	u64 bar1_length;
	u32 max_instance;
	u32 ecc_supported;
	u64 fb_reservation;
};

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
	struct nvidia_vgpu_type *vgpu_type;
	u32 vm_pid;
};

/**
 * struct nvidia_vgpu_chid - per-vGPU channel IDs
 *
 * @chid_offset: beginning offset of channel IDs
 * @num_chid: number of allocated channel IDs
 * @num_plugin_channels: number of channels for vGPU manager
 */
struct nvidia_vgpu_chid {
	u32 chid_offset;
	u32 num_chid;
	u32 num_plugin_channels;
};

struct nvidia_vgpu_mgmt {
	struct nvidia_vgpu_mem *heap_mem;
	void __iomem *ctrl_vaddr;
	void __iomem *init_task_log_vaddr;
	void __iomem *vgpu_task_log_vaddr;
	void __iomem *kernel_log_vaddr;
};

struct nvidia_vgpu_rpc {
	/* RPC channel lock */
	struct mutex lock;
	u32 msg_seq_num;
	void __iomem *ctrl_buf;
	void __iomem *resp_buf;
	void __iomem *msg_buf;
	void __iomem *migration_buf;
	void __iomem *error_buf;
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
 * @gsp_client: per-vGPU GSP client
 * @chid: vGPU channel IDs
 * @fbmem_heap: allocated FB memory for the vGPU
 * @mgmt: vGPU mgmt heap
 * @rpc: vGPU host RPC
 */
struct nvidia_vgpu {
	/* Per-vGPU lock */
	struct mutex lock;
	struct pci_dev *pdev;
	atomic_t status;
	struct list_head vgpu_list;

	struct nvidia_vgpu_info info;
	struct nvidia_vgpu_mgr *vgpu_mgr;
	struct nvidia_vgpu_gsp_client gsp_client;

	struct nvidia_vgpu_chid chid;
	struct nvidia_vgpu_mem *fbmem_heap;
	struct nvidia_vgpu_mgmt mgmt;
	struct nvidia_vgpu_rpc rpc;
};

struct nvidia_vgpu_event_listener {
	int (*func)(struct nvidia_vgpu_event_listener *self, unsigned int event, void *data);
	struct list_head list;
};

struct nvidia_vgpu_event_chain {
	/* lock for PF event listener list */
	struct mutex lock;
	struct list_head head;
};

/**
 * struct nvidia_vgpu_mgr - the vGPU manager
 *
 * @refcount: the reference count
 * @handle: the driver handle
 * @total_avail_chids: total available channel IDs
 * @total_fbmem_size: total FB memory size
 * @vmmu_segment_size: VMMU segment size
 * @ecc_enabled: ECC is enabled in the GPU
 * @comm_buff_size: communication buffer size of mgmt heap
 * @init_task_log_offset: offset of init task log in mgmt heap
 * @init_task_log_size: size of init task size in mgmt heap
 * @vgpu_task_log_size: size of vgpu task log size in mgmt heap
 * @kernel_log_size: size of kernel log size in mgmt heap
 * @vgpu_major: vGPU major version
 * @vgpu_minor: vGPU minor version
 * @vgpu_list_lock: lock to protect vGPU list
 * @vgpu_list_head: list head of vGPU list
 * @num_vgpus: number of vGPUs in the vGPU list
 * @gsp_client: the GSP client
 * @vgpu_types: installed vGPU types
 * @num_vgpu_types: number of installed vGPU types
 * @use_alloc_bitmap: use chid allocator for the PF driver doesn't support chid allocation
 * @chid_alloc_bitmap: chid allocator bitmap
 * @curr_vgpu_lock: lock to protect curr_vgpu_type
 * @curr_vgpu_type: type of current created vgpu in homogeneous mode
 * @num_instances: number of created vGPU with curr_vgpu_type in homogeneous mode
 * @pf_driver_event_chain: PF driver event chain
 * @pdev: the PCI device pointer
 * @bar0_vaddr: the virtual address of BAR0
 */
struct nvidia_vgpu_mgr {
	struct kref refcount;
	struct nvidia_vgpu_mgr_handle handle;

	/* core driver configurations */
	u32 total_avail_chids;
	u64 total_fbmem_size;
	void *engine_bitmap;

	/* GSP RM configurations */
	u64 vmmu_segment_size;
	bool ecc_enabled;

	/* GSP RM constraints */
	u64 comm_buff_size;
	u64 init_task_log_offset;
	u64 init_task_log_size;
	u64 vgpu_task_log_size;
	u64 kernel_log_size;

	u64 vgpu_major;
	u64 vgpu_minor;

	/* lock for vGPU list */
	struct mutex vgpu_list_lock;
	struct list_head vgpu_list_head;
	atomic_t num_vgpus;

	struct nvidia_vgpu_gsp_client gsp_client;
	struct nvidia_vgpu_type *vgpu_types;
	unsigned int num_vgpu_types;

	bool use_chid_alloc_bitmap;
	void *chid_alloc_bitmap;

	/* lock for current vGPU type */
	struct mutex curr_vgpu_type_lock;
	struct nvidia_vgpu_type *curr_vgpu_type;
	unsigned int num_instances;

	struct nvidia_vgpu_event_chain pf_driver_event_chain;

	struct pci_dev *pdev;
	void __iomem *bar0_vaddr;
};

#define nvidia_vgpu_mgr_for_each_vgpu(vgpu, vgpu_mgr) \
	list_for_each_entry((vgpu), &(vgpu_mgr)->vgpu_list_head, vgpu_list)

int nvidia_vgpu_mgr_setup(struct pci_dev *dev, int (*init_vfio_fn)(void *priv, void *data),
			  void *init_vfio_fn_data);
void nvidia_vgpu_mgr_release(struct nvidia_vgpu_mgr *vgpu_mgr);
void nvidia_vgpu_event_init_chain(struct nvidia_vgpu_event_chain *chain);
void nvidia_vgpu_event_register_listener(struct nvidia_vgpu_event_chain *chain,
					 struct nvidia_vgpu_event_listener *l);
void nvidia_vgpu_event_unregister_listener(struct nvidia_vgpu_event_chain *chain,
					   struct nvidia_vgpu_event_listener *l);

int nvidia_vgpu_mgr_destroy_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_create_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_reset_vgpu(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_setup_metadata(struct nvidia_vgpu_mgr *vgpu_mgr);
void nvidia_vgpu_mgr_clean_metadata(struct nvidia_vgpu_mgr *vgpu_mgr);
int nvidia_vgpu_rpc_call(struct nvidia_vgpu *vgpu, u32 msg_type,
			 void *data, u64 size);
void nvidia_vgpu_clean_rpc(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_setup_rpc(struct nvidia_vgpu *vgpu);
int nvidia_vgpu_mgr_set_bme(struct nvidia_vgpu *vgpu, bool enable);

#endif
