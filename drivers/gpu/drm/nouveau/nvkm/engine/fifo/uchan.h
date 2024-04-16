/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UCHAN_H__
#define __NVKM_UCHAN_H__
#include <engine/fifo.h>
#include <nvif/driverif.h>

int nvkm_uchan_new(struct nvkm_device *, struct nvkm_cgrp *, u8 runl, u8 runq, bool priv, u16 devm,
		   struct nvkm_vmm *, struct nvif_ctxdma_priv *push, u64 offset, u64 length,
		   struct nvif_mem_priv *userd, u16 userd_offset, const char *name,
		   const struct nvif_chan_impl **, struct nvif_chan_priv **, struct nvkm_object **);
#endif
