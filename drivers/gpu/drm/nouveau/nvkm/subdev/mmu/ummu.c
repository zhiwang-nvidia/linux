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

static int
nvkm_ummu_vmm_new(struct nvif_mmu_priv *ummu, enum nvif_vmm_type type, u64 addr, u64 size,
		  void *argv, u32 argc, const struct nvif_vmm_impl **pimpl,
		  struct nvif_vmm_priv **ppriv, u64 handle)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uvmm_new(ummu->mmu, type, addr, size, argv, argc, pimpl, ppriv, &object);
	if (ret)
		return ret;

	return nvkm_object_link_rb(ummu->object.client, &ummu->object, handle, object);
}

static int
nvkm_ummu_mem_new(struct nvif_mmu_priv *ummu, u8 type, u8 page, u64 size, void *argv, u32 argc,
		  const struct nvif_mem_impl **pimpl, struct nvif_mem_priv **ppriv, u64 handle)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_umem_new(ummu->mmu, type, page, size, argv, argc, pimpl, ppriv, &object);
	if (ret)
		return ret;

	return nvkm_object_link_rb(ummu->object.client, &ummu->object, handle, object);
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

static const struct nvkm_object_func
nvkm_ummu = {
};

int
nvkm_ummu_new(struct nvkm_device *device, const struct nvif_mmu_impl **pimpl,
	      struct nvif_mmu_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_mmu *mmu = device->mmu;
	struct nvif_mmu_priv *ummu;
	int kinds = 0;

	if (!(ummu = kzalloc(sizeof(*ummu), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_ummu, &(struct nvkm_oclass) {}, &ummu->object);
	ummu->mmu = mmu;
	ummu->impl = nvkm_ummu_impl;

	ummu->impl.dmabits = mmu->dma_bits;
	ummu->impl.heap_nr = mmu->heap_nr;
	ummu->impl.type_nr = mmu->type_nr;

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

	if (mmu->func->kind) {
		ummu->impl.kind = mmu->func->kind(mmu, &kinds, &ummu->impl.kind_inv);
		ummu->impl.kind_nr = kinds;
	}

	ummu->impl.mem.oclass = mmu->func->mem.user.oclass;
	ummu->impl.mem.new = nvkm_ummu_mem_new;

	ummu->impl.vmm.oclass = mmu->func->vmm.user.oclass;
	ummu->impl.vmm.new = nvkm_ummu_vmm_new;

	*pimpl = &ummu->impl;
	*ppriv = ummu;
	*pobject = &ummu->object;
	return 0;
}
