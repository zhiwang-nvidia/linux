/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DEVICE_H__
#define __NVIF_DEVICE_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
#include <nvif/user.h>
struct nvif_ctxdma;

struct nvif_device {
	const struct nvif_device_impl *impl;
	struct nvif_device_priv *priv;
	struct nvif_object object;
	struct nvif_map map;

	struct nvif_user user;
};

int  nvif_device_ctor(struct nvif_client *, const char *name, struct nvif_device *);
void nvif_device_dtor(struct nvif_device *);
int  nvif_device_map(struct nvif_device *);
u64  nvif_device_time(struct nvif_device *);

int nvif_device_ctxdma_ctor(struct nvif_device *, const char *name, s32 oclass,
			    void *argv, u32 argc, struct nvif_ctxdma *);
#endif
