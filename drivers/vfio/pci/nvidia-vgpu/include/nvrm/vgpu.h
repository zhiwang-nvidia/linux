/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#ifndef __NVRM_VGPU_H__
#define __NVRM_VGPU_H__

#include <nvrm/nv_vgpu_types.h>

#define VMIOPD_MAX_INSTANCES 16
#define VMIOPD_MAX_HEADS     4

#define GSP_PLUGIN_BOOTLOADED 0x4E654A6F

/*
 *   GSP Plugin heap memory layout
 * +--------------------------------+ offset = 0
 * |         CONTROL BUFFER         |
 * +--------------------------------+
 * |        RESPONSE BUFFER         |
 * +--------------------------------+
 * |         MESSAGE BUFFER         |
 * +--------------------------------+
 * |        MIGRATION BUFFER        |
 * +--------------------------------+
 * |    GSP PLUGIN ERROR BUFFER     |
 * +--------------------------------+
 * |    INIT TASK LOG BUFFER        |
 * +--------------------------------+
 * |    VGPU TASK LOG BUFFER        |
 * +--------------------------------+
 * |       KERNEL LOG BUFFER        |
 * +--------------------------------+
 * |      MEMORY AVAILABLE FOR      |
 * | GSP PLUGIN INTERNAL HEAP USAGE |
 * +--------------------------------+
 */
#define VGPU_CPU_GSP_CTRL_BUFF_VERSION              0x1
#define VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE          4096
#define VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE      4096
#define VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE       4096
#define VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE     (2 * 1024 * 1024)
#define VGPU_CPU_GSP_ERROR_BUFF_REGION_SIZE         4096
#define VGPU_CPU_GSP_INIT_TASK_LOG_BUFF_REGION_SIZE (128 * 1024)
#define VGPU_CPU_GSP_VGPU_TASK_LOG_BUFF_REGION_SIZE (256 * 1024)
#define VGPU_CPU_GSP_KERNEL_TASK_LOG_BUFF_REGION_SIZE (64 * 1024)
#define VGPU_CPU_GSP_COMMUNICATION_BUFF_TOTAL_SIZE  (VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_ERROR_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_INIT_TASK_LOG_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_VGPU_TASK_LOG_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_KERNEL_TASK_LOG_BUFF_REGION_SIZE)

typedef union {
	NvU8 buf[VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE];
	struct {
		NvU32  version;
		NvU32  message_type;
		NvU32  message_seq_num;
		NvU64  response_buff_offset;
		NvU64  message_buff_offset;
		NvU64  migration_buff_offset;
		NvU64  error_buff_offset;
		NvU32  migration_buf_cpu_access_offset;
		NvBool is_migration_in_progress;
		NvU32  error_buff_cpu_get_idx;
		NvU32 attached_vgpu_count;
		struct {
			NvU32 vgpu_type_id;
			NvU32 host_gpu_pci_id;
			NvU32 pci_dev_id;
			NvU8  vgpu_uuid[VM_UUID_SIZE];
		} host_info[VMIOPD_MAX_INSTANCES];
	};
} VGPU_CPU_GSP_CTRL_BUFF_REGION;

enum {
	NV_VGPU_CPU_RPC_MSG_VERSION_NEGOTIATION = 1,
	NV_VGPU_CPU_RPC_MSG_SETUP_CONFIG_PARAMS_AND_INIT,
	NV_VGPU_CPU_RPC_MSG_RESET,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_STOP_WORK,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_CANCEL_STOP,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_SAVE_STATE,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_CANCEL_SAVE,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_RESTORE_STATE,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_RESTORE_DEFERRED_STATE,
	NV_VGPU_CPU_RPC_MSG_MIGRATION_RESUME_WORK,
	NV_VGPU_CPU_RPC_MSG_CONSOLE_VNC_STATE,
	NV_VGPU_CPU_RPC_MSG_VF_BAR0_REG_ACCESS,
	NV_VGPU_CPU_RPC_MSG_UPDATE_BME_STATE,
	NV_VGPU_CPU_RPC_MSG_GET_GUEST_INFO,
	NV_VGPU_CPU_RPC_MSG_MAX,
};

typedef struct {
	NvU32 version_cpu;
	NvU32 version_negotiated;
} NV_VGPU_CPU_RPC_DATA_VERSION_NEGOTIATION;

typedef struct {
	NvU8   vgpu_uuid[VM_UUID_SIZE];
	NvU32  dbdf;
	NvU32  driver_vm_vf_dbdf;
	NvU32  vgpu_device_instance_id;
	NvU32  vgpu_type;
	NvU32  vm_pid;
	NvU32  swizz_id;
	NvU32  num_channels;
	NvU32  num_plugin_channels;
	NvU32  vmm_cap;
	NvU32  migration_feature;
	NvU32  hypervisor_type;
	NvU32  host_cpu_arch;
	NvU64  host_page_size;
	NvBool rev1[3];
	NvBool enable_uvm;
	NvBool linux_interrupt_optimization;
	NvBool vmm_migration_supported;
	NvBool rev2;
	NvBool enable_console_vnc;
	NvBool use_non_stall_linux_events;
	NvBool rev3[3];
	NvU16  placement_id;
	NvU32  rev4;
	NvU32  channel_usage_threshold_percentage;
	NvBool rev5;
	NvU32  rev6;
	NvBool rev7;
} NV_VGPU_CPU_RPC_DATA_COPY_CONFIG_PARAMS;

typedef struct {
	NvBool enable;
	NvBool allowed;
} NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE;

typedef union {
	NvU8 buf[VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE];
	NV_VGPU_CPU_RPC_DATA_VERSION_NEGOTIATION    version_data;
	NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE       bme_state;
} VGPU_CPU_GSP_MSG_BUFF_REGION;

typedef struct {
	NvU64 sequence_update_start;
	NvU64 sequence_update_end;
	NvU32 effective_fb_page_size;
	NvU32 rect_width;
	NvU32 rect_height;
	NvU32 surface_width;
	NvU32 surface_height;
	NvU32 surface_size;
	NvU32 surface_offset;
	NvU32 surface_format;
	NvU32 surface_kind;
	NvU32 surface_pitch;
	NvU32 surface_type;
	NvU8  surface_block_height;
	NvBool is_blanking_enabled;
	NvBool is_flip_pending;
	NvBool is_free_pending;
	NvBool is_memory_blocklinear;
} VGPU_CPU_GSP_DISPLAYLESS_SURFACE;

typedef union {
	NvU8 buf[VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE];
	struct {
		NvU32  message_seq_num_received;
		NvU32  message_seq_num_processed;
		NvU32  result_code;
		NvU32  guest_rpc_version;
		NvU32  migration_buf_gsp_access_offset;
		NvU32  migration_state_save_complete;
		VGPU_CPU_GSP_DISPLAYLESS_SURFACE surface[VMIOPD_MAX_HEADS];
		NvU32  error_buff_gsp_put_idx;
		NvU32  grid_license_state;
		NvU32  guest_os_type;
		NvU32  frl_config;
	};
} VGPU_CPU_GSP_RESPONSE_BUFF_REGION;

#endif
