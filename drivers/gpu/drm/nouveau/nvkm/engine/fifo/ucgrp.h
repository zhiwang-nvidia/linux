/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UCGRP_H__
#define __NVKM_UCGRP_H__
#include <engine/fifo.h>
#include <nvif/driverif.h>

int nvkm_ucgrp_new(struct nvkm_fifo *, u8 runl, struct nvif_vmm_priv *,  const char *name,
		   const struct nvif_cgrp_impl **, struct nvif_cgrp_priv **, struct nvkm_object **);
#endif
