#ifndef __NVIF_MMU_H__
#define __NVIF_MMU_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
struct nvif_device;

struct nvif_mmu {
	const struct nvif_mmu_impl *impl;
	struct nvif_mmu_priv *priv;
	struct nvif_object object;
};

int nvif_mmu_ctor(struct nvif_device *, const char *name, struct nvif_mmu *);
void nvif_mmu_dtor(struct nvif_mmu *);

static inline bool
nvif_mmu_kind_valid(struct nvif_mmu *mmu, u8 kind)
{
	if (kind) {
		if (kind >= mmu->impl->kind_nr || mmu->impl->kind[kind] == mmu->impl->kind_inv)
			return false;
	}
	return true;
}

static inline int
nvif_mmu_type(struct nvif_mmu *mmu, u8 mask)
{
	int i;
	for (i = 0; i < mmu->impl->type_nr; i++) {
		if ((mmu->impl->type[i].type & mask) == mask)
			return i;
	}
	return -EINVAL;
}
#endif
