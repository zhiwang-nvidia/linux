#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrla081_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrla081_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2014-2020 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <nvrm/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h>

#define NVA081_CTRL_VGPU_CONFIG_INVALID_TYPE 0x00
#define NVA081_MAX_VGPU_TYPES_PER_PGPU       0x40
#define NVA081_MAX_VGPU_PER_PGPU             32
#define NVA081_VM_UUID_SIZE                  16
#define NVA081_VGPU_STRING_BUFFER_SIZE       32
#define NVA081_VGPU_SIGNATURE_SIZE           128
#define NVA081_VM_NAME_SIZE                  128
#define NVA081_PCI_CONFIG_SPACE_SIZE         0x100
#define NVA081_PGPU_METADATA_STRING_SIZE     256
#define NVA081_EXTRA_PARAMETERS_SIZE         1024

/*
 * NVA081_CTRL_VGPU_CONFIG_INFO
 *
 * This structure represents the per vGPU information
 *
 */
typedef struct NVA081_CTRL_VGPU_INFO {
	// This structure should be in sync with NVA082_CTRL_CMD_HOST_VGPU_DEVICE_GET_VGPU_TYPE_INFO_PARAMS
	NvU32 vgpuType;
	NvU8  vgpuName[NVA081_VGPU_STRING_BUFFER_SIZE];
	NvU8  vgpuClass[NVA081_VGPU_STRING_BUFFER_SIZE];
	NvU8  vgpuSignature[NVA081_VGPU_SIGNATURE_SIZE];
	NvU8  license[NV_GRID_LICENSE_INFO_MAX_LENGTH];
	NvU32 maxInstance;
	NvU32 numHeads;
	NvU32 maxResolutionX;
	NvU32 maxResolutionY;
	NvU32 maxPixels;
	NvU32 frlConfig;
	NvU32 cudaEnabled;
	NvU32 eccSupported;
	NvU32 gpuInstanceSize;
	NvU32 multiVgpuSupported;
	NV_DECLARE_ALIGNED(NvU64 vdevId, 8);
	NV_DECLARE_ALIGNED(NvU64 pdevId, 8);
	NV_DECLARE_ALIGNED(NvU64 profileSize, 8);
	NV_DECLARE_ALIGNED(NvU64 fbLength, 8);
	NV_DECLARE_ALIGNED(NvU64 gspHeapSize, 8);
	NV_DECLARE_ALIGNED(NvU64 fbReservation, 8);
	NV_DECLARE_ALIGNED(NvU64 mappableVideoSize, 8);
	NvU32 encoderCapacity;
	NV_DECLARE_ALIGNED(NvU64 bar1Length, 8);
	NvU32 frlEnable;
	NvU8  adapterName[NV2080_GPU_MAX_NAME_STRING_LENGTH];
	NvU16 adapterName_Unicode[NV2080_GPU_MAX_NAME_STRING_LENGTH];
	NvU8  shortGpuNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
	NvU8  licensedProductName[NV_GRID_LICENSE_INFO_MAX_LENGTH];
	NvU32 vgpuExtraParams[NVA081_EXTRA_PARAMETERS_SIZE];
	NvU32 ftraceEnable;
	NvU32 gpuDirectSupported;
	NvU32 nvlinkP2PSupported;
	NvU32 multiVgpuExclusive;
	NvU32 exclusiveType;
	NvU32 exclusiveSize;
	// used only by NVML
	NvU32 gpuInstanceProfileId;
} NVA081_CTRL_VGPU_INFO;

/*
 * NVA081_CTRL_VGPU_CONFIG_INFO_PARAMS
 *
 * This structure represents the vGPU configuration information
 *
 */
#define NVA081_CTRL_VGPU_CONFIG_INFO_PARAMS_MESSAGE_ID (0x1U)

typedef struct NVA081_CTRL_VGPU_CONFIG_INFO_PARAMS {
	NvBool discardVgpuTypes;
	NV_DECLARE_ALIGNED(NVA081_CTRL_VGPU_INFO vgpuInfo, 8);
	NvU32  vgpuConfigState;
} NVA081_CTRL_VGPU_CONFIG_INFO_PARAMS;

/* VGPU Config state values */
#define NVA081_CTRL_VGPU_CONFIG_STATE_UNINITIALIZED         0
#define NVA081_CTRL_VGPU_CONFIG_STATE_IN_PROGRESS           1
#define NVA081_CTRL_VGPU_CONFIG_STATE_READY                 2

#endif
