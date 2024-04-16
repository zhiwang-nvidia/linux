/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UCAPS_H__
#define __NVKM_UCAPS_H__
#include "priv.h"
#include <nvif/driverif.h>

int nvkm_ucaps_new(struct nvkm_disp *, const struct nvif_disp_caps_impl **,
		   struct nvif_disp_caps_priv **, struct nvkm_object **);
#endif
