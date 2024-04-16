/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CTXDMA_H__
#define __NVIF_CTXDMA_H__
#include <nvif/object.h>
#include <nvif/driverif.h>

struct nvif_ctxdma {
	const struct nvif_ctxdma_impl *impl;
	struct nvif_ctxdma_priv *priv;
	struct nvif_object object;
};

static inline void
nvif_ctxdma_ctor(struct nvif_object *parent, const char *name, u32 handle, s32 oclass,
		 struct nvif_ctxdma *ctxdma)
{
	nvif_object_ctor(parent, name, handle, oclass, &ctxdma->object);
}

static inline void
nvif_ctxdma_dtor(struct nvif_ctxdma *ctxdma)
{
	if (!ctxdma->impl)
		return;

	ctxdma->impl->del(ctxdma->priv);
	ctxdma->impl = NULL;
}
#endif
