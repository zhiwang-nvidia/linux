/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UVFN_H__
#define __NVKM_UVFN_H__
#include <subdev/vfn.h>
#include <nvif/driverif.h>

int nvkm_uvfn_new(struct nvkm_device *, const struct nvif_usermode_impl **,
		  struct nvif_usermode_priv **, struct nvkm_object **);
#endif
