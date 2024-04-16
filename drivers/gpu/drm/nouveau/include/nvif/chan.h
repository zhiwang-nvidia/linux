/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CHAN_H__
#define __NVIF_CHAN_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
#include <nvif/push.h>
struct nvif_cgrp;
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
#endif
