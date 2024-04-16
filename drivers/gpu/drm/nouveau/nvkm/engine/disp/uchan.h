/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UDISP_CHAN_H__
#define __NVKM_UDISP_CHAN_H__
#include "chan.h"
#include <nvif/driverif.h>

int nvkm_disp_chan_new(struct nvkm_disp *disp, const struct nvkm_disp_func_chan *, u8 id,
		       struct nvkm_memory *, const struct nvif_disp_chan_impl **,
		       struct nvif_disp_chan_priv **, struct nvkm_object **);
#endif
