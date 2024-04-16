#ifndef __NVKM_UMEM_H__
#define __NVKM_UMEM_H__
#include "mem.h"

int nvkm_umem_new(const struct nvkm_oclass *, void *argv, u32 argc,
		  struct nvkm_object **);
#endif
