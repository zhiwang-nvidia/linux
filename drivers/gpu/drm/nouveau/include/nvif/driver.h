/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DRIVER_H__
#define __NVIF_DRIVER_H__
#include <nvif/os.h>
struct nvif_parent;
struct nvif_driver;
struct nvif_client_impl;
struct nvif_client_priv;
struct nvif_client;

void nvif_driver_ctor(struct nvif_parent *, const struct nvif_driver *, const char *name,
		      const struct nvif_client_impl *, struct nvif_client_priv *,
		      struct nvif_client *);
#endif
