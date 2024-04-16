#ifndef __NVKM_UMMU_H__
#define __NVKM_UMMU_H__
#include <core/object.h>
#include "priv.h"

#define nvkm_ummu nvif_mmu_priv

struct nvif_mmu_priv {
	struct nvkm_object object;
	struct nvkm_mmu *mmu;
};

int nvkm_ummu_new(struct nvkm_device *, const struct nvkm_oclass *,
		  void *argv, u32 argc, struct nvkm_object **);
#endif
