#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080vgpumgrinternal_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080vgpumgrinternal_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE
 *
 * This command is used to add a new vGPU config to the pGPU in physical RM.
 * Unlike NVA081_CTRL_CMD_VGPU_CONFIG_SET_INFO, it does no validation
 * and is only to be used internally.
 *
 * discardVgpuTypes [IN]
 *  This parameter specifies if existing vGPU configuration should be
 *  discarded for given pGPU
 *
 * vgpuInfoCount [IN]
 *   This parameter specifies the number of entries of virtual GPU type
 *   information
 *
 * vgpuInfo [IN]
 *   This parameter specifies virtual GPU type information
 *
 * Possible status values returned are:
 *   NV_OK
 *   NV_ERR_OBJECT_NOT_FOUND
 *   NV_ERR_NOT_SUPPORTED
 */
#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE (0x20804003) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_VGPU_MGR_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE_PARAMS_MESSAGE_ID (0x3U)

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE_PARAMS {
	NvBool discardVgpuTypes;
	NvU32  vgpuInfoCount;
	NV_DECLARE_ALIGNED(NVA081_CTRL_VGPU_INFO vgpuInfo[NVA081_MAX_VGPU_TYPES_PER_PGPU], 8);
} NV2080_CTRL_VGPU_MGR_INTERNAL_PGPU_ADD_VGPU_TYPE_PARAMS;

/*
 * NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK
 *
 * This command is used to bootload GSP VGPU plugin task.
 * Can be called only with SR-IOV and with VGPU_GSP_PLUGIN_OFFLOAD feature.
 *
 * dbdf                        - domain (31:16), bus (15:8), device (7:3), function (2:0)
 * gfid                        - Gfid
 * vgpuType                    - The Type ID for VGPU profile
 * vmPid                       - Plugin process ID of vGPU guest instance
 * swizzId                     - SwizzId
 * numChannels                 - Number of channels
 * numPluginChannels           - Number of plugin channels
 * bDisableSmcPartitionRestore - If set to true, SMC default execution partition
 *                               save/restore will not be done in host-RM
 * guestFbPhysAddrList         - list of VMMU segment aligned physical address of guest FB memory
 * guestFbLengthList           - list of guest FB memory length in bytes
 * pluginHeapMemoryPhysAddr    - plugin heap memory offset
 * pluginHeapMemoryLength      - plugin heap memory length in bytes
 * bDeviceProfilingEnabled     - If set to true, profiling is allowed
 */
#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK (0x20804001) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_VGPU_MGR_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_MAX_VMMU_SEGMENTS                                   384

/* Must match NV2080_ENGINE_TYPE_LAST from cl2080.h */
#define NV2080_GPU_MAX_ENGINES                                          0x3e

#define NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS_MESSAGE_ID (0x1U)

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
	NvBool bDeviceProfilingEnabled;
} NV2080_CTRL_VGPU_MGR_INTERNAL_BOOTLOAD_GSP_VGPU_PLUGIN_TASK_PARAMS;

/*
 * NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK
 *
 * This command is used to shutdown GSP VGPU plugin task.
 * Can be called only with SR-IOV and with VGPU_GSP_PLUGIN_OFFLOAD feature.
 *
 * gfid                        - Gfid
 */
#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK (0x20804002) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_VGPU_MGR_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS_MESSAGE_ID (0x2U)

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS {
	NvU32 gfid;
} NV2080_CTRL_VGPU_MGR_INTERNAL_SHUTDOWN_GSP_VGPU_PLUGIN_TASK_PARAMS;

/*
 * NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP
 *
 * This command is used to cleanup all the GSP VGPU plugin task allocated resources after its shutdown.
 * Can be called only with SR-IOV and with VGPU_GSP_PLUGIN_OFFLOAD feature.
 *
 * gfid [IN]
 *  This parameter specifies the gfid of vGPU assigned to VM.
 *
 * Possible status values returned are:
 *   NV_OK
 *   NV_ERR_NOT_SUPPORTED
 */
#define NV2080_CTRL_CMD_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP (0x20804008) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_VGPU_MGR_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS_MESSAGE_ID (0x8U)

typedef struct NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS {
	NvU32 gfid;
} NV2080_CTRL_VGPU_MGR_INTERNAL_VGPU_PLUGIN_CLEANUP_PARAMS;

#endif
