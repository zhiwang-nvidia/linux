/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */
#ifndef __src_common_sdk_vmioplugin_inc_vmioplugin_h__
#define __src_common_sdk_vmioplugin_inc_vmioplugin_h__

#define VMIOPD_MAX_INSTANCES 16
#define VMIOPD_MAX_HEADS     4

/**
 * Boolean type.
 */

enum vmiop_bool_e {
	vmiop_false = 0,        /*!< Boolean false */
	vmiop_true = 1          /*!< Boolean true */
};

/**
 * Boolean type.
 */

typedef enum vmiop_bool_e vmiop_bool_t;

#endif
