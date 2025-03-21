/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_VGPU_TYPES_H__
#define __NVRM_VGPU_TYPES_H__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.124.04 */

#include <nvrm/nvtypes.h>

#define VM_UUID_SIZE            16
#define INVALID_VGPU_DEV_INST   0xFFFFFFFFU
#define MAX_VGPU_DEVICES_PER_VM 16U

/* This enum represents the current state of guest dependent fields */
typedef enum GUEST_VM_INFO_STATE {
	GUEST_VM_INFO_STATE_UNINITIALIZED = 0,
	GUEST_VM_INFO_STATE_INITIALIZED = 1,
} GUEST_VM_INFO_STATE;

/* This enum represents types of VM identifiers */
typedef enum VM_ID_TYPE {
	VM_ID_DOMAIN_ID = 0,
	VM_ID_UUID = 1,
} VM_ID_TYPE;

/* This structure represents VM identifier */
typedef union VM_ID {
	NvU8 vmUuid[VM_UUID_SIZE];
	NV_DECLARE_ALIGNED(NvU64 vmId, 8);
} VM_ID;

#endif
