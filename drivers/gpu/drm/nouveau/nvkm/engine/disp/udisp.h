/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UDISP_H__
#define __NVKM_UDISP_H__
#include <core/object.h>
#include "priv.h"

struct nvif_disp_priv {
	struct nvkm_object object;
	struct nvkm_disp *disp;
};
#endif
