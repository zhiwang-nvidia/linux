/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_BOOTLOAD_H__
#define __NVRM_BOOTLOAD_H__

#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.124.04 */

#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK (0x20804001)

#define NV2080_CTRL_MAX_VMMU_SEGMENTS                                   384

/* Must match NV2080_ENGINE_TYPE_LAST from cl2080.h */
#define NV2080_GPU_MAX_ENGINES                                          0x54

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS {
	NvU32  dbdf;
	NvU32  gfid;
	NvU32  vgpuType;
	NvU32  vmPid;
	NvU32  swizzId;
	NvU32  numChannels;
	NvU32  numPluginChannels;
	NvU32  chidOffset[NV2080_GPU_MAX_ENGINES];
	NvBool bDisableDefaultSmcExecPartRestore;
	NvU32  numGuestFbSegments;
	NV_DECLARE_ALIGNED(NvU64 guestFbPhysAddrList[NV2080_CTRL_MAX_VMMU_SEGMENTS], 8);
	NV_DECLARE_ALIGNED(NvU64 guestFbLengthList[NV2080_CTRL_MAX_VMMU_SEGMENTS], 8);
	NV_DECLARE_ALIGNED(NvU64 pluginHeapMemoryPhysAddr, 8);
	NV_DECLARE_ALIGNED(NvU64 pluginHeapMemoryLength, 8);
	NV_DECLARE_ALIGNED(NvU64 ctrlBuffOffset, 8);
	NV_DECLARE_ALIGNED(NvU64 initTaskLogBuffOffset, 8);
	NV_DECLARE_ALIGNED(NvU64 initTaskLogBuffSize, 8);
	NV_DECLARE_ALIGNED(NvU64 vgpuTaskLogBuffOffset, 8);
	NV_DECLARE_ALIGNED(NvU64 vgpuTaskLogBuffSize, 8);
	NV_DECLARE_ALIGNED(NvU64 kernelLogBuffOffset, 8);
	NV_DECLARE_ALIGNED(NvU64 kernelLogBuffSize, 8);
	NV_DECLARE_ALIGNED(NvU64 migRmHeapMemoryPhysAddr, 8);
	NV_DECLARE_ALIGNED(NvU64 migRmHeapMemoryLength, 8);
	NvBool bDeviceProfilingEnabled;
} NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS;

#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK (0x20804002)

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS {
	NvU32 gfid;
} NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS;

#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP (0x20804008)

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS {
	NvU32 gfid;
} NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS;

#endif
