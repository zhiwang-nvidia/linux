#ifndef __NVKM_UVMM_H__
#define __NVKM_UVMM_H__
#include "vmm.h"

int nvkm_uvmm_new(const struct nvkm_oclass *, void *argv, u32 argc,
		  struct nvkm_object **);
#endif
