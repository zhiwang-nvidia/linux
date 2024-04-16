/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DRIVER_H__
#define __NVKM_DRIVER_H__
#include <nvif/driverif.h>
struct nvkm_device;

int nvkm_driver_ctor(struct nvkm_device *, const struct nvif_driver **,
		     const struct nvif_client_impl **, struct nvif_client_priv **);
#endif
