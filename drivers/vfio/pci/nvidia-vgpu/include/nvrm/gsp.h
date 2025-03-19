/* SPDX-License-Identifier: MIT */
#ifndef __NVRM_GSP_H__
#define __NVRM_GSP_H__

#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570 */

#define NV2080_CTRL_CMD_GSP_GET_FEATURES (0x20803601)

typedef struct NV2080_CTRL_GSP_GET_FEATURES_PARAMS {
	NvU32  gspFeatures;
	NvBool bValid;
	NvBool bDefaultGspRmGpu;
	NvU8   firmwareVersion[GSP_MAX_BUILD_VERSION_LENGTH];
} NV2080_CTRL_GSP_GET_FEATURES_PARAMS;

#endif
