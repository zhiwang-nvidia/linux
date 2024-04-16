#ifndef __NVKM_UMMU_H__
#define __NVKM_UMMU_H__
#include <core/object.h>
struct nvkm_device;

#include <nvif/driverif.h>

#define nvkm_ummu nvif_mmu_priv

struct nvif_mmu_priv {
	struct nvkm_object object;
	struct nvkm_mmu *mmu;

	struct nvif_mmu_impl impl;
};

int nvkm_ummu_new(struct nvkm_device *, const struct nvif_mmu_impl **, struct nvif_mmu_priv **,
		  struct nvkm_object **);
#endif
