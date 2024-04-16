/*
 * Copyright 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ummu.h"
#include "umem.h"
#include "uvmm.h"

#include <core/client.h>

#include <nvif/if0008.h>
#include <nvif/unpack.h>

static int
nvkm_ummu_sclass(struct nvkm_object *object, int index,
		 struct nvkm_oclass *oclass)
{
	struct nvkm_mmu *mmu = container_of(object, struct nvif_mmu_priv, object)->mmu;

	if (mmu->func->mem.user.oclass) {
		if (index-- == 0) {
			oclass->base = mmu->func->mem.user;
			oclass->ctor = nvkm_umem_new;
			return 0;
		}
	}

	if (mmu->func->vmm.user.oclass) {
		if (index-- == 0) {
			oclass->base = mmu->func->vmm.user;
			oclass->ctor = nvkm_uvmm_new;
			return 0;
		}
	}

	return -EINVAL;
}

static int
nvkm_ummu_kind(struct nvkm_ummu *ummu, void *argv, u32 argc)
{
	struct nvkm_mmu *mmu = ummu->mmu;
	union {
		struct nvif_mmu_kind_v0 v0;
	} *args = argv;
	const u8 *kind = NULL;
	int ret = -ENOSYS, count = 0;
	u8 kind_inv = 0;

	if (mmu->func->kind)
		kind = mmu->func->kind(mmu, &count, &kind_inv);

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, true))) {
		if (argc != args->v0.count * sizeof(*args->v0.data))
			return -EINVAL;
		if (args->v0.count > count)
			return -EINVAL;
		args->v0.kind_inv = kind_inv;
		memcpy(args->v0.data, kind, args->v0.count);
	} else
		return ret;

	return 0;
}

static void
nvkm_ummu_del(struct nvif_mmu_priv *ummu)
{
	struct nvkm_object *object = &ummu->object;

	nvkm_object_del(&object);
}

static const struct nvif_mmu_impl
nvkm_ummu_impl = {
	.del = nvkm_ummu_del,
};

static int
nvkm_ummu_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvif_mmu_priv *ummu = container_of(object, typeof(*ummu), object);
	switch (mthd) {
	case NVIF_MMU_V0_KIND: return nvkm_ummu_kind(ummu, argv, argc);
	default:
		break;
	}
	return -EINVAL;
}

static const struct nvkm_object_func
nvkm_ummu = {
	.mthd = nvkm_ummu_mthd,
	.sclass = nvkm_ummu_sclass,
};

int
nvkm_ummu_new(struct nvkm_device *device, const struct nvif_mmu_impl **pimpl,
	      struct nvif_mmu_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_mmu *mmu = device->mmu;
	struct nvif_mmu_priv *ummu;
	int kinds = 0;
	u8 unused = 0;

	if (!(ummu = kzalloc(sizeof(*ummu), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_ummu, &(struct nvkm_oclass) {}, &ummu->object);
	ummu->mmu = mmu;
	ummu->impl = nvkm_ummu_impl;

	if (mmu->func->kind)
		mmu->func->kind(mmu, &kinds, &unused);

	ummu->impl.dmabits = mmu->dma_bits;
	ummu->impl.heap_nr = mmu->heap_nr;
	ummu->impl.type_nr = mmu->type_nr;
	ummu->impl.kind_nr = kinds;

	for (int i = 0; i < mmu->heap_nr; i++)
		ummu->impl.heap[i].size = mmu->heap[i].size;

	for (int i = 0; i < mmu->type_nr; i++) {
		u8 type = mmu->type[i].type;

		ummu->impl.type[i].type |= (type & NVKM_MEM_VRAM) ? NVIF_MEM_VRAM : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_HOST) ? NVIF_MEM_HOST : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_COMP) ? NVIF_MEM_COMP : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_DISP) ? NVIF_MEM_DISP : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_KIND) ? NVIF_MEM_KIND : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_MAPPABLE) ? NVIF_MEM_MAPPABLE : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_COHERENT) ? NVIF_MEM_COHERENT : 0;
		ummu->impl.type[i].type |= (type & NVKM_MEM_UNCACHED) ? NVIF_MEM_UNCACHED : 0;
		ummu->impl.type[i].heap = mmu->type[i].heap;
	}

	ummu->impl.mem.oclass = mmu->func->mem.user.oclass;

	ummu->impl.vmm.oclass = mmu->func->vmm.user.oclass;

	*pimpl = &ummu->impl;
	*ppriv = ummu;
	*pobject = &ummu->object;
	return 0;
}
