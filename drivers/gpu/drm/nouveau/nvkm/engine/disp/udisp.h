/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UDISP_H__
#define __NVKM_UDISP_H__
#include <core/object.h>
#include "priv.h"
#include <nvif/driverif.h>

struct nvif_disp_priv {
	struct nvkm_object object;
	struct nvkm_disp *disp;

	struct nvif_disp_impl impl;
};

int nvkm_udisp_new(struct nvkm_device *, const struct nvif_disp_impl **, struct nvif_disp_priv **,
		   struct nvkm_object **);
#endif
