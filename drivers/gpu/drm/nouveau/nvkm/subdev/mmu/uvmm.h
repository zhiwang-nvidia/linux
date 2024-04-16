#ifndef __NVKM_UVMM_H__
#define __NVKM_UVMM_H__
#include "vmm.h"
#include <nvif/driverif.h>

int nvkm_uvmm_new(struct nvkm_mmu *mmu, u8 type, u64 addr, u64 size,
		  void *, u32, const struct nvif_vmm_impl **, struct nvif_vmm_priv **,
		  struct nvkm_object **);
#endif
