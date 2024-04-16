#ifndef __NVKM_UMEM_H__
#define __NVKM_UMEM_H__
#include "mem.h"
#include <nvif/driverif.h>

int nvkm_umem_new(struct nvkm_mmu *, u8 type, u8 page, u64 size, void *, u32,
		  const struct nvif_mem_impl **, struct nvif_mem_priv **, struct nvkm_object **);
struct nvkm_memory *nvkm_umem_ref(struct nvif_mem_priv *);
#endif
