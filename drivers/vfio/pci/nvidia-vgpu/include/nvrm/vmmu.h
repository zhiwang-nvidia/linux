/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_VMMU_H__
#define __NVRM_VMMU_H__

#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.124.04 */

/*
 * NV2080_CTRL_CMD_GPU_GET_VMMU_SEGMENT_SIZE
 *
 * This command returns the VMMU page size
 *
 *   vmmuSegmentSize
 *     Output parameter.
 *     Returns the VMMU segment size (in bytes)
 *
 * Possible status values returned are:
 *   NV_OK
 *   NV_ERR_NOT_SUPPORTED
 */
#define NV2080_CTRL_CMD_GPU_GET_VMMU_SEGMENT_SIZE  (0x2080017eU) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_GPU_INTERFACE_ID << 8) | NV2080_CTRL_GPU_GET_VMMU_SEGMENT_SIZE_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_GPU_GET_VMMU_SEGMENT_SIZE_PARAMS_MESSAGE_ID (0x7EU)

typedef struct NV2080_CTRL_GPU_GET_VMMU_SEGMENT_SIZE_PARAMS {
	NV_DECLARE_ALIGNED(NvU64 vmmuSegmentSize, 8);
} NV2080_CTRL_GPU_GET_VMMU_SEGMENT_SIZE_PARAMS;

#define NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_32MB     0x02000000U
#define NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_64MB     0x04000000U
#define NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_128MB    0x08000000U
#define NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_256MB    0x10000000U
#define NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_512MB    0x20000000U

#endif
