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
#include "uvmm.h"
#include "umem.h"
#include "ummu.h"

#include <core/client.h>
#include <core/memory.h>

#include <nvif/if000c.h>
#include <nvif/unpack.h>

struct nvif_vmm_priv {
	struct nvkm_object object;
	struct nvkm_vmm *vmm;

	struct nvif_vmm_impl impl;
};

static const struct nvkm_object_func nvkm_uvmm;
struct nvkm_vmm *
nvkm_uvmm_search(struct nvkm_client *client, u64 handle)
{
	struct nvkm_object *object;

	object = nvkm_object_search(client, handle, &nvkm_uvmm);
	if (IS_ERR(object))
		return (void *)object;

	return nvkm_vmm_ref(container_of(object, struct nvif_vmm_priv, object)->vmm);
}

static int
nvkm_uvmm_pfnclr(struct nvif_vmm_priv *uvmm, u64 addr, u64 size)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	int ret = 0;

	if (nvkm_vmm_in_managed_range(vmm, addr, size) && vmm->managed.raw)
		return -EINVAL;

	if (size) {
		mutex_lock(&vmm->mutex.vmm);
		ret = nvkm_vmm_pfn_unmap(vmm, addr, size);
		mutex_unlock(&vmm->mutex.vmm);
	}

	return ret;
}

static int
nvkm_uvmm_pfnmap(struct nvif_vmm_priv *uvmm, u8 page, u64 addr, u64 size, u64 *phys)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	int ret = 0;

	if (nvkm_vmm_in_managed_range(vmm, addr, size) && vmm->managed.raw)
		return -EINVAL;

	if (size) {
		mutex_lock(&vmm->mutex.vmm);
		ret = nvkm_vmm_pfn_map(vmm, page, addr, size, phys);
		mutex_unlock(&vmm->mutex.vmm);
	}

	return ret;
}

static int
nvkm_uvmm_unmap(struct nvif_vmm_priv *uvmm, u64 addr)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	int ret;

	if (nvkm_vmm_in_managed_range(vmm, addr, 0) && vmm->managed.raw)
		return -EINVAL;

	mutex_lock(&vmm->mutex.vmm);
	vma = nvkm_vmm_node_search(vmm, addr);
	if (ret = -ENOENT, !vma || vma->addr != addr) {
		VMM_DEBUG(vmm, "lookup %016llx: %016llx",
			  addr, vma ? vma->addr : ~0ULL);
		goto done;
	}

	if (ret = -ENOENT, vma->busy) {
		VMM_DEBUG(vmm, "denied %016llx: %d", addr, vma->busy);
		goto done;
	}

	if (ret = -EINVAL, !vma->memory) {
		VMM_DEBUG(vmm, "unmapped");
		goto done;
	}

	nvkm_vmm_unmap_locked(vmm, vma, false);
	ret = 0;
done:
	mutex_unlock(&vmm->mutex.vmm);
	return ret;
}

static int
nvkm_uvmm_map(struct nvif_vmm_priv *uvmm, u64 addr, u64 size, void *argv, u32 argc,
	      struct nvif_mem_priv *mem, u64 offset)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	struct nvkm_memory *memory;
	int ret;

	if (nvkm_vmm_in_managed_range(vmm, addr, size) && vmm->managed.raw)
		return -EINVAL;

	memory = nvkm_umem_ref(mem);

	mutex_lock(&vmm->mutex.vmm);
	if (ret = -ENOENT, !(vma = nvkm_vmm_node_search(vmm, addr))) {
		VMM_DEBUG(vmm, "lookup %016llx", addr);
		goto fail;
	}

	if (ret = -ENOENT, vma->busy) {
		VMM_DEBUG(vmm, "denied %016llx: %d", addr, vma->busy);
		goto fail;
	}

	if (ret = -EINVAL, vma->mapped && !vma->memory) {
		VMM_DEBUG(vmm, "pfnmap %016llx", addr);
		goto fail;
	}

	if (ret = -EINVAL, vma->addr != addr || vma->size != size) {
		if (addr + size > vma->addr + vma->size || vma->memory ||
		    (vma->refd == NVKM_VMA_PAGE_NONE && !vma->mapref)) {
			VMM_DEBUG(vmm, "split %d %d %d "
				       "%016llx %016llx %016llx %016llx",
				  !!vma->memory, vma->refd, vma->mapref,
				  addr, size, vma->addr, (u64)vma->size);
			goto fail;
		}

		vma = nvkm_vmm_node_split(vmm, vma, addr, size);
		if (!vma) {
			ret = -ENOMEM;
			goto fail;
		}
	}
	vma->busy = true;
	mutex_unlock(&vmm->mutex.vmm);

	ret = nvkm_memory_map(memory, offset, vmm, vma, argv, argc);
	if (ret == 0) {
		/* Successful map will clear vma->busy. */
		nvkm_memory_unref(&memory);
		return 0;
	}

	mutex_lock(&vmm->mutex.vmm);
	vma->busy = false;
	nvkm_vmm_unmap_region(vmm, vma);
fail:
	mutex_unlock(&vmm->mutex.vmm);
	nvkm_memory_unref(&memory);
	return ret;
}

static int
nvkm_uvmm_put(struct nvif_vmm_priv *uvmm, u64 addr)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	int ret;

	mutex_lock(&vmm->mutex.vmm);
	vma = nvkm_vmm_node_search(vmm, addr);
	if (ret = -ENOENT, !vma || vma->addr != addr || vma->part) {
		VMM_DEBUG(vmm, "lookup %016llx: %016llx %d", addr,
			  vma ? vma->addr : ~0ULL, vma ? vma->part : 0);
		goto done;
	}

	if (ret = -ENOENT, vma->busy) {
		VMM_DEBUG(vmm, "denied %016llx: %d", addr, vma->busy);
		goto done;
	}

	nvkm_vmm_put_locked(vmm, vma);
	ret = 0;
done:
	mutex_unlock(&vmm->mutex.vmm);
	return ret;
}

static int
nvkm_uvmm_get(struct nvif_vmm_priv *uvmm, enum nvif_vmm_get_type type, bool sparse,
	      u8 page, u8 align, u64 size, u64 *addr)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	bool getref = type == NVIF_VMM_GET_PTES;
	bool mapref = type == NVIF_VMM_GET_ADDR;
	int ret;

	mutex_lock(&vmm->mutex.vmm);
	ret = nvkm_vmm_get_locked(vmm, getref, mapref, sparse,
				  page, align, size, &vma);
	mutex_unlock(&vmm->mutex.vmm);
	if (ret)
		return ret;

	*addr = vma->addr;
	return 0;
}

static inline int
nvkm_uvmm_page_index(struct nvif_vmm_priv *uvmm, u64 size, u8 shift, u8 *refd)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	const struct nvkm_vmm_page *page;

	if (likely(shift)) {
		for (page = vmm->func->page; page->shift; page++) {
			if (shift == page->shift)
				break;
		}

		if (!page->shift || !IS_ALIGNED(size, 1ULL << page->shift)) {
			VMM_DEBUG(vmm, "page %d %016llx", shift, size);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	*refd = page - vmm->func->page;

	return 0;
}

static int
nvkm_uvmm_raw_get(struct nvif_vmm_priv *uvmm, u8 shift, u64 addr, u64 size)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, addr, size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, size, shift, &refd);
	if (ret)
		return ret;

	return nvkm_vmm_raw_get(vmm, addr, size, refd);
}

static int
nvkm_uvmm_raw_put(struct nvif_vmm_priv *uvmm, u8 shift, u64 addr, u64 size)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, addr, size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, size, shift, &refd);
	if (ret)
		return ret;

	nvkm_vmm_raw_put(vmm, addr, size, refd);

	return 0;
}

static int
nvkm_uvmm_raw_map(struct nvif_vmm_priv *uvmm, u8 shift, u64 addr, u64 size, void *argv, u32 argc,
		  struct nvif_mem_priv *umem, u64 offset)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma vma = {
		.addr = addr,
		.size = size,
		.used = true,
		.mapref = false,
		.no_comp = true,
	};
	struct nvkm_memory *memory;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, addr, size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, size, shift, &refd);
	if (ret)
		return ret;

	vma.page = vma.refd = refd;

	memory = nvkm_umem_ref(umem);

	ret = nvkm_memory_map(memory, offset, vmm, &vma, argv, argc);

	nvkm_memory_unref(&vma.memory);
	nvkm_memory_unref(&memory);
	return ret;
}

static int
nvkm_uvmm_raw_unmap(struct nvif_vmm_priv *uvmm, u8 shift, u64 addr, u64 size, bool sparse)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, addr, size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, size, shift, &refd);
	if (ret)
		return ret;

	nvkm_vmm_raw_unmap(vmm, addr, size, sparse, refd);
	return 0;
}

static int
nvkm_uvmm_raw_sparse(struct nvif_vmm_priv *uvmm, u64 addr, u64 size, bool ref)
{
	struct nvkm_vmm *vmm = uvmm->vmm;

	if (!nvkm_vmm_in_managed_range(vmm, addr, size))
		return -EINVAL;

	return nvkm_vmm_raw_sparse(vmm, addr, size, ref);
}

static int
nvkm_uvmm_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvif_vmm_priv *uvmm = container_of(object, typeof(*uvmm), object);
	switch (mthd) {
	case NVIF_VMM_V0_MTHD(0x00) ... NVIF_VMM_V0_MTHD(0x7f):
		if (uvmm->vmm->func->mthd) {
			return uvmm->vmm->func->mthd(uvmm->vmm,
						     uvmm->object.client,
						     mthd, argv, argc);
		}
		break;
	default:
		break;
	}
	return -EINVAL;
}

static void
nvkm_uvmm_del(struct nvif_vmm_priv *uvmm)
{
	struct nvkm_object *object = &uvmm->object;

	nvkm_object_del(&object);
}

static const struct nvif_vmm_impl
nvkm_uvmm_impl = {
	.del = nvkm_uvmm_del,
	.get = nvkm_uvmm_get,
	.put = nvkm_uvmm_put,
	.map = nvkm_uvmm_map,
	.unmap = nvkm_uvmm_unmap,
	.pfnmap = nvkm_uvmm_pfnmap,
	.pfnclr = nvkm_uvmm_pfnclr,
	.raw.get = nvkm_uvmm_raw_get,
	.raw.put = nvkm_uvmm_raw_put,
	.raw.map = nvkm_uvmm_raw_map,
	.raw.unmap = nvkm_uvmm_raw_unmap,
	.raw.sparse = nvkm_uvmm_raw_sparse,
};

static void *
nvkm_uvmm_dtor(struct nvkm_object *object)
{
	struct nvif_vmm_priv *uvmm = container_of(object, typeof(*uvmm), object);

	nvkm_vmm_unref(&uvmm->vmm);
	return uvmm;
}

static const struct nvkm_object_func
nvkm_uvmm = {
	.dtor = nvkm_uvmm_dtor,
	.mthd = nvkm_uvmm_mthd,
};

int
nvkm_uvmm_new(struct nvkm_mmu *mmu, u8 type, u64 addr, u64 size, void *argv, u32 argc,
	      const struct nvif_vmm_impl **pimpl, struct nvif_vmm_priv **ppriv,
	      struct nvkm_object **pobject)
{
	const struct nvkm_vmm_page *page;
	struct nvif_vmm_priv *uvmm;
	int ret = -ENOSYS;
	bool managed, raw;

	managed = type == NVIF_VMM_TYPE_MANAGED;
	raw = type == NVIF_VMM_TYPE_RAW;

	if (!(uvmm = kzalloc(sizeof(*uvmm), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uvmm, &(struct nvkm_oclass) {}, &uvmm->object);

	if (!mmu->vmm) {
		ret = mmu->func->vmm.ctor(mmu, managed || raw, addr, size,
					  argv, argc, NULL, "user", &uvmm->vmm);
		if (ret)
			return ret;

		uvmm->vmm->debug = NV_DBG_ERROR;
	} else {
		if (size)
			return -EINVAL;

		uvmm->vmm = nvkm_vmm_ref(mmu->vmm);
	}
	uvmm->vmm->managed.raw = raw;

	if (mmu->func->promote_vmm) {
		ret = mmu->func->promote_vmm(uvmm->vmm);
		if (ret)
			return ret;
	}

	uvmm->impl = nvkm_uvmm_impl;
	uvmm->impl.start = uvmm->vmm->start;
	uvmm->impl.limit = uvmm->vmm->limit;

	page = uvmm->vmm->func->page;
	for (int i = 0; page->shift; i++, page++) {
		if (WARN_ON(i >= ARRAY_SIZE(uvmm->impl.page)))
			break;

		uvmm->impl.page[i].shift  = page->shift;
		uvmm->impl.page[i].sparse = !!(page->type & NVKM_VMM_PAGE_SPARSE);
		uvmm->impl.page[i].vram   = !!(page->type & NVKM_VMM_PAGE_VRAM);
		uvmm->impl.page[i].host   = !!(page->type & NVKM_VMM_PAGE_HOST);
		uvmm->impl.page[i].comp   = !!(page->type & NVKM_VMM_PAGE_COMP);
		uvmm->impl.page_nr++;
	}

	*pimpl = &uvmm->impl;
	*ppriv = uvmm;
	*pobject = &uvmm->object;
	return 0;
}
