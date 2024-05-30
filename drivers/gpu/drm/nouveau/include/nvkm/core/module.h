/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MODULE_H__
#define __NVKM_MODULE_H__
#include <linux/module.h>

int __init nvkm_init(void);
void __exit nvkm_exit(void);

extern char *nvkm_cfg;
extern char *nvkm_dbg;
extern int nvkm_runpm;
#endif
