/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_USER_H__
#define __NVKM_DEVICE_USER_H__
#include <core/device.h>
#include <nvif/driverif.h>

int nvkm_udevice_new(struct nvkm_device *, const struct nvif_device_impl **,
		     struct nvif_device_priv **, struct nvkm_object **);
#endif
