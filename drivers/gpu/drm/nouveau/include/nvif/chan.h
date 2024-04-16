/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CHAN_H__
#define __NVIF_CHAN_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
#include <nvif/event.h>
#include <nvif/push.h>
struct nvif_cgrp;
struct nvif_ctxdma;
struct nvif_device;

struct nvif_chan {
	const struct nvif_chan_impl *impl;
	struct nvif_chan_priv *priv;
	struct nvif_object object;

	struct nvif_device *device;
	u8 runl;
	u8 runq;

	struct nvif_push push;
};

void nvif_chan_ctor(struct nvif_device *, struct nvif_cgrp *, const char *name, u8 runl, u8 runq,
		    struct nvif_chan *);
void nvif_chan_dtor(struct nvif_chan *);

int nvif_chan_event_ctor(struct nvif_chan *, const char *name,
			 int (*ctor)(struct nvif_chan_priv *, u64 token,
				     const struct nvif_event_impl **, struct nvif_event_priv **),
			 nvif_event_func, struct nvif_event *);

int nvif_chan_ctxdma_ctor(struct nvif_chan *, const char *name, u32 handle, s32 oclass,
			  void *argv, u32 argc, struct nvif_ctxdma *);

struct nvif_engobj {
	const struct nvif_engobj_impl *impl;
	struct nvif_engobj_priv *priv;
	struct nvif_object object;
};

int nvif_engobj_ctor(struct nvif_chan *chan, const char *name, u32 handle, s32 oclass,
		     struct nvif_engobj *);
void nvif_engobj_dtor(struct nvif_engobj *);
#endif
