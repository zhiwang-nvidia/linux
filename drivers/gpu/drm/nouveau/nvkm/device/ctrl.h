/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_CTRL_H__
#define __NVKM_DEVICE_CTRL_H__
#include <core/device.h>
#include <nvif/driverif.h>

int nvkm_control_new(struct nvkm_device *, const struct nvif_control_impl **,
		     struct nvif_control_priv **, struct nvkm_object **);
#endif
