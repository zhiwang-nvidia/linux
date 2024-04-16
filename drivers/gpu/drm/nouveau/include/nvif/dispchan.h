/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DISPCHAN_H__
#define __NVIF_DISPCHAN_H__
#include "disp.h"
#include "push.h"

struct nvif_dispchan {
	const struct nvif_disp_chan_impl *impl;
	struct nvif_disp_chan_priv *priv;
	struct nvif_object object;
	struct nvif_map map;

	struct nvif_disp *disp;
	struct nvif_push push;
};

int nvif_dispchan_ctor(struct nvif_disp *, const char *name, u32 handle, s32 oclass,
		       struct nvif_mmu *, struct nvif_dispchan *);
int nvif_dispchan_oneinit(struct nvif_dispchan *);
void nvif_dispchan_dtor(struct nvif_dispchan *);
#endif
