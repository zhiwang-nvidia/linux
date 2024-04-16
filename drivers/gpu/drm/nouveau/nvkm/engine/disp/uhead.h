/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UHEAD_H__
#define __NVKM_UHEAD_H__
#include "head.h"
#include <nvif/driverif.h>

int nvkm_uhead_new(struct nvkm_disp *, u8 id, const struct nvif_head_impl **,
		   struct nvif_head_priv **, struct nvkm_object **);
#endif
