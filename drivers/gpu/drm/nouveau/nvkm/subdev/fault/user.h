/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UFAULT_H__
#define __NVKM_UFAULT_H__
#include <subdev/fault.h>
#include <nvif/driverif.h>

int nvkm_ufault_new(struct nvkm_device *, const struct nvif_faultbuf_impl **,
		    struct nvif_faultbuf_priv **, struct nvkm_object **);
#endif
