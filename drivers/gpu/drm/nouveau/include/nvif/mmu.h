#ifndef __NVIF_MMU_H__
#define __NVIF_MMU_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
struct nvif_device;

struct nvif_mmu {
	const struct nvif_mmu_impl *impl;
	struct nvif_mmu_priv *priv;
	struct nvif_object object;
	u8  kind_inv;
	u16 kind_nr;

	u8 *kind;
};

int nvif_mmu_ctor(struct nvif_device *, const char *name, struct nvif_mmu *);
void nvif_mmu_dtor(struct nvif_mmu *);

static inline bool
nvif_mmu_kind_valid(struct nvif_mmu *mmu, u8 kind)
{
	if (kind) {
		if (kind >= mmu->kind_nr || mmu->kind[kind] == mmu->kind_inv)
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
