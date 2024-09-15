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

#endif
