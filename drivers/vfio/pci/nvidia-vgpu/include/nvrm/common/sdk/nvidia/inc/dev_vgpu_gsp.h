/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */
#ifndef __src_common_sdk_nvidia_inc_vgpu_dev_nv_vgpu_gsp_h__
#define __src_common_sdk_nvidia_inc_vgpu_dev_nv_vgpu_gsp_h__

#include "nv_vgpu_types.h"

#define GSP_PLUGIN_BOOTLOADED 0x4E654A6F

/******************************************************************************/
/* GSP Control buffer shared between CPU Plugin and GSP Plugin - START        */
/******************************************************************************/

/*    GSP Plugin heap memory layout
      +--------------------------------+ offset = 0
      |         CONTROL BUFFER         |
      +--------------------------------+
      |        RESPONSE BUFFER         |
      +--------------------------------+
      |         MESSAGE BUFFER         |
      +--------------------------------+
      |        MIGRATION BUFFER        |
      +--------------------------------+
      |    GSP PLUGIN ERROR BUFFER     |
      +--------------------------------+
      |    INIT TASK LOG BUFFER        |
      +--------------------------------+
      |    VGPU TASK LOG BUFFER        |
      +--------------------------------+
      |      MEMORY AVAILABLE FOR      |
      | GSP PLUGIN INTERNAL HEAP USAGE |
      +--------------------------------+
 */
#define VGPU_CPU_GSP_CTRL_BUFF_VERSION              0x1
#define VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE          4096
#define VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE      4096
#define VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE       4096
#define VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE     (2 * 1024 * 1024)
#define VGPU_CPU_GSP_ERROR_BUFF_REGION_SIZE         4096
#define VGPU_CPU_GSP_INIT_TASK_LOG_BUFF_REGION_SIZE (128 * 1024)
#define VGPU_CPU_GSP_VGPU_TASK_LOG_BUFF_REGION_SIZE (256 * 1024)
#define VGPU_CPU_GSP_COMMUNICATION_BUFF_TOTAL_SIZE  (VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE          + \
		VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE      + \
		VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE       + \
		VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE     + \
		VGPU_CPU_GSP_ERROR_BUFF_REGION_SIZE         + \
		VGPU_CPU_GSP_INIT_TASK_LOG_BUFF_REGION_SIZE + \
		VGPU_CPU_GSP_VGPU_TASK_LOG_BUFF_REGION_SIZE)

//
// Control buffer: CPU Plugin -> GSP Plugin
// CPU Plugin - Write only
// GSP Plugin - Read only
//
typedef union {
	NvU8 buf[VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE];
	struct {
		volatile NvU32  version;                        // Version of format
		volatile NvU32  message_type;                   // Task to be performed by GSP Plugin
		volatile NvU32  message_seq_num;                // Incrementing sequence number to identify the RPC packet
		volatile NvU64  response_buff_offset;           // Buffer used to send data from GSP Plugin -> CPU Plugin
		volatile NvU64  message_buff_offset;            // Buffer used to send RPC data between CPU and GSP Plugin
		volatile NvU64  migration_buff_offset;          // Buffer used to send migration data between CPU and GSP Plugin
		volatile NvU64  error_buff_offset;              // Buffer used to send error data from GSP Plugin -> CPU Plugin
		volatile NvU32  migration_buf_cpu_access_offset;// CPU plugin GET/PUT offset of migration buffer
		volatile NvBool is_migration_in_progress;       // Is migration active or cancelled
		volatile NvU32  error_buff_cpu_get_idx;         // GET pointer into ERROR Buffer for CPU Plugin
		volatile NvU32 attached_vgpu_count;
		volatile struct {
			NvU32 vgpu_type_id;
			NvU32 host_gpu_pci_id;
			NvU32 pci_dev_id;
			NvU8  vgpu_uuid[VM_UUID_SIZE];
		} host_info[VMIOPD_MAX_INSTANCES];
	};
} VGPU_CPU_GSP_CTRL_BUFF_REGION;

//
// Specify actions intended on getting
// notification from CPU Plugin -> GSP plugin
//
typedef enum {
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
} MESSAGE;

//
// Params structure for NV_VGPU_CPU_RPC_MSG_VERSION_NEGOTIATION
//
typedef struct {
	volatile NvU32 version_cpu;        /* Sent by CPU Plugin */
	volatile NvU32 version_negotiated; /* Updated by GSP Plugin */
} NV_VGPU_CPU_RPC_DATA_VERSION_NEGOTIATION;

//
// Host CPU arch
//
typedef enum {
	NV_VGPU_HOST_CPU_ARCH_AARCH64 = 1,
	NV_VGPU_HOST_CPU_ARCH_X86_64,
} NV_VGPU_HOST_CPU_ARCH;

//
// Params structure for NV_VGPU_CPU_RPC_MSG_COPY_CONFIG_PARAMS
//
typedef struct {
	volatile NvU8   vgpu_uuid[VM_UUID_SIZE];
	volatile NvU32  dbdf;
	volatile NvU32  driver_vm_vf_dbdf;
	volatile NvU32  vgpu_device_instance_id;
	volatile NvU32  vgpu_type;
	volatile NvU32  vm_pid;
	volatile NvU32  swizz_id;
	volatile NvU32  num_channels;
	volatile NvU32  num_plugin_channels;
	volatile NvU32  vmm_cap;
	volatile NvU32  migration_feature;
	volatile NvU32  hypervisor_type;
	volatile NvU32  host_cpu_arch;
	volatile NvU64  host_page_size;
	volatile NvBool rev1[2];
	volatile NvBool enable_uvm;
	volatile NvBool linux_interrupt_optimization;
	volatile NvBool vmm_migration_supported;
	volatile NvBool rev2;
	volatile NvBool enable_console_vnc;
	volatile NvBool use_non_stall_linux_events;
	volatile NvU32  rev3;
} NV_VGPU_CPU_RPC_DATA_COPY_CONFIG_PARAMS;

// Params structure for NV_VGPU_CPU_RPC_MSG_UPDATE_BME_STATE
typedef struct {
	volatile NvBool enable;
	volatile NvBool allowed;
} NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE;
//
// Message Buffer:
// CPU Plugin - Read/Write
// GSP Plugin - Read/Write
//
typedef union {
	NvU8 buf[VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE];
	NV_VGPU_CPU_RPC_DATA_VERSION_NEGOTIATION    version_data;
	NV_VGPU_CPU_RPC_DATA_COPY_CONFIG_PARAMS     config_data;
	NV_VGPU_CPU_RPC_DATA_UPDATE_BME_STATE       bme_state;
} VGPU_CPU_GSP_MSG_BUFF_REGION;

typedef struct {
	volatile NvU64                          sequence_update_start;
	volatile NvU64                          sequence_update_end;
	volatile NvU32                          effective_fb_page_size;
	volatile NvU32                          rect_width;
	volatile NvU32                          rect_height;
	volatile NvU32                          surface_width;
	volatile NvU32                          surface_height;
	volatile NvU32                          surface_size;
	volatile NvU32                          surface_offset;
	volatile NvU32                          surface_format;
	volatile NvU32                          surface_kind;
	volatile NvU32                          surface_pitch;
	volatile NvU32                          surface_type;
	volatile NvU8                           surface_block_height;
	volatile vmiop_bool_t                   is_blanking_enabled;
	volatile vmiop_bool_t                   is_flip_pending;
	volatile vmiop_bool_t                   is_free_pending;
	volatile vmiop_bool_t                   is_memory_blocklinear;
} VGPU_CPU_GSP_DISPLAYLESS_SURFACE;

//
// GSP Plugin Response Buffer:
// CPU Plugin - Read only
// GSP Plugin - Write only
//
typedef union {
	NvU8 buf[VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE];
	struct {
		// Updated by GSP Plugin once task is complete
		volatile NvU32                              message_seq_num_processed;
		// Updated by GSP on completion of RPC
		volatile NvU32                              result_code;
		volatile NvU32                              guest_rpc_version;
		// GSP plugin GET/PUT offset pointer of migration buffer
		volatile NvU32                              migration_buf_gsp_access_offset;
		// Current state of migration
		volatile NvU32                              migration_state_save_complete;
		// Console VNC surface information
		volatile VGPU_CPU_GSP_DISPLAYLESS_SURFACE   surface[VMIOPD_MAX_HEADS];
		// PUT pointer into ERROR Buffer for GSP Plugin
		volatile NvU32                              error_buff_gsp_put_idx;
		// Updated grid license state as received from guest
		volatile NvU32                              grid_license_state;
	};
} VGPU_CPU_GSP_RESPONSE_BUFF_REGION;

/******************************************************************************/
/* GSP Control buffer shared between CPU Plugin and GSP Plugin - END          */
/******************************************************************************/
#endif
