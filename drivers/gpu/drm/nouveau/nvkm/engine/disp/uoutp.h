/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UOUTP_H__
#define __NVKM_UOUTP_H__
#include "outp.h"
#include <nvif/driverif.h>

int nvkm_uoutp_new(struct nvkm_disp *, u8 id, const struct nvif_outp_impl **,
		   struct nvif_outp_priv **, struct nvkm_object **);
#endif
