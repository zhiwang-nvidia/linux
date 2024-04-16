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

#define nvkm_uvmm nvif_vmm_priv

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
nvkm_uvmm_mthd_pfnclr(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_pfnclr_v0 v0;
	} *args = argv;
	struct nvkm_vmm *vmm = uvmm->vmm;
	int ret = -ENOSYS;
	u64 addr, size;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		addr = args->v0.addr;
		size = args->v0.size;
	} else
		return ret;

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
nvkm_uvmm_mthd_pfnmap(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_pfnmap_v0 v0;
	} *args = argv;
	struct nvkm_vmm *vmm = uvmm->vmm;
	int ret = -ENOSYS;
	u64 addr, size, *phys;
	u8  page;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, true))) {
		page = args->v0.page;
		addr = args->v0.addr;
		size = args->v0.size;
		phys = args->v0.phys;
		if (argc != (size >> page) * sizeof(args->v0.phys[0]))
			return -EINVAL;
	} else
		return ret;

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
nvkm_uvmm_mthd_unmap(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_unmap_v0 v0;
	} *args = argv;
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	int ret = -ENOSYS;
	u64 addr;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		addr = args->v0.addr;
	} else
		return ret;

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
nvkm_uvmm_mthd_map(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	struct nvkm_client *client = uvmm->object.client;
	union {
		struct nvif_vmm_map_v0 v0;
	} *args = argv;
	u64 addr, size, handle, offset;
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	struct nvkm_memory *memory;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, true))) {
		addr = args->v0.addr;
		size = args->v0.size;
		handle = args->v0.memory;
		offset = args->v0.offset;
	} else
		return ret;

	if (nvkm_vmm_in_managed_range(vmm, addr, size) && vmm->managed.raw)
		return -EINVAL;

	memory = nvkm_umem_search(vmm->mmu, client, handle);
	if (IS_ERR(memory)) {
		VMM_DEBUG(vmm, "memory %016llx %ld\n", handle, PTR_ERR(memory));
		return PTR_ERR(memory);
	}

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
nvkm_uvmm_mthd_put(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_put_v0 v0;
	} *args = argv;
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	int ret = -ENOSYS;
	u64 addr;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		addr = args->v0.addr;
	} else
		return ret;

	mutex_lock(&vmm->mutex.vmm);
	vma = nvkm_vmm_node_search(vmm, args->v0.addr);
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
nvkm_uvmm_mthd_get(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_get_v0 v0;
	} *args = argv;
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma *vma;
	int ret = -ENOSYS;
	bool getref, mapref, sparse;
	u8 page, align;
	u64 size;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		getref = args->v0.type == NVIF_VMM_GET_V0_PTES;
		mapref = args->v0.type == NVIF_VMM_GET_V0_ADDR;
		sparse = args->v0.sparse;
		page = args->v0.page;
		align = args->v0.align;
		size = args->v0.size;
	} else
		return ret;

	mutex_lock(&vmm->mutex.vmm);
	ret = nvkm_vmm_get_locked(vmm, getref, mapref, sparse,
				  page, align, size, &vma);
	mutex_unlock(&vmm->mutex.vmm);
	if (ret)
		return ret;

	args->v0.addr = vma->addr;
	return ret;
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
nvkm_uvmm_mthd_raw_get(struct nvkm_uvmm *uvmm, struct nvif_vmm_raw_v0 *args)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, args->addr, args->size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, args->size, args->shift, &refd);
	if (ret)
		return ret;

	return nvkm_vmm_raw_get(vmm, args->addr, args->size, refd);
}

static int
nvkm_uvmm_mthd_raw_put(struct nvkm_uvmm *uvmm, struct nvif_vmm_raw_v0 *args)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, args->addr, args->size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, args->size, args->shift, &refd);
	if (ret)
		return ret;

	nvkm_vmm_raw_put(vmm, args->addr, args->size, refd);

	return 0;
}

static int
nvkm_uvmm_mthd_raw_map(struct nvkm_uvmm *uvmm, struct nvif_vmm_raw_v0 *args)
{
	struct nvkm_client *client = uvmm->object.client;
	struct nvkm_vmm *vmm = uvmm->vmm;
	struct nvkm_vma vma = {
		.addr = args->addr,
		.size = args->size,
		.used = true,
		.mapref = false,
		.no_comp = true,
	};
	struct nvkm_memory *memory;
	void *argv = (void *)(uintptr_t)args->argv;
	unsigned int argc = args->argc;
	u64 handle = args->memory;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, args->addr, args->size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, args->size, args->shift, &refd);
	if (ret)
		return ret;

	vma.page = vma.refd = refd;

	memory = nvkm_umem_search(uvmm->vmm->mmu, client, args->memory);
	if (IS_ERR(memory)) {
		VMM_DEBUG(vmm, "memory %016llx %ld\n", handle, PTR_ERR(memory));
		return PTR_ERR(memory);
	}

	ret = nvkm_memory_map(memory, args->offset, vmm, &vma, argv, argc);

	nvkm_memory_unref(&vma.memory);
	nvkm_memory_unref(&memory);
	return ret;
}

static int
nvkm_uvmm_mthd_raw_unmap(struct nvkm_uvmm *uvmm, struct nvif_vmm_raw_v0 *args)
{
	struct nvkm_vmm *vmm = uvmm->vmm;
	u8 refd;
	int ret;

	if (!nvkm_vmm_in_managed_range(vmm, args->addr, args->size))
		return -EINVAL;

	ret = nvkm_uvmm_page_index(uvmm, args->size, args->shift, &refd);
	if (ret)
		return ret;

	nvkm_vmm_raw_unmap(vmm, args->addr, args->size,
			   args->sparse, refd);

	return 0;
}

static int
nvkm_uvmm_mthd_raw_sparse(struct nvkm_uvmm *uvmm, struct nvif_vmm_raw_v0 *args)
{
	struct nvkm_vmm *vmm = uvmm->vmm;

	if (!nvkm_vmm_in_managed_range(vmm, args->addr, args->size))
		return -EINVAL;

	return nvkm_vmm_raw_sparse(vmm, args->addr, args->size, args->ref);
}

static int
nvkm_uvmm_mthd_raw(struct nvkm_uvmm *uvmm, void *argv, u32 argc)
{
	union {
		struct nvif_vmm_raw_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;

	if (!uvmm->vmm->managed.raw)
		return -EINVAL;

	if ((ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, true)))
		return ret;

	switch (args->v0.op) {
	case NVIF_VMM_RAW_V0_GET:
		return nvkm_uvmm_mthd_raw_get(uvmm, &args->v0);
	case NVIF_VMM_RAW_V0_PUT:
		return nvkm_uvmm_mthd_raw_put(uvmm, &args->v0);
	case NVIF_VMM_RAW_V0_MAP:
		return nvkm_uvmm_mthd_raw_map(uvmm, &args->v0);
	case NVIF_VMM_RAW_V0_UNMAP:
		return nvkm_uvmm_mthd_raw_unmap(uvmm, &args->v0);
	case NVIF_VMM_RAW_V0_SPARSE:
		return nvkm_uvmm_mthd_raw_sparse(uvmm, &args->v0);
	default:
		return -EINVAL;
	};
}

static int
nvkm_uvmm_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvif_vmm_priv *uvmm = container_of(object, typeof(*uvmm), object);
	switch (mthd) {
	case NVIF_VMM_V0_GET   : return nvkm_uvmm_mthd_get   (uvmm, argv, argc);
	case NVIF_VMM_V0_PUT   : return nvkm_uvmm_mthd_put   (uvmm, argv, argc);
	case NVIF_VMM_V0_MAP   : return nvkm_uvmm_mthd_map   (uvmm, argv, argc);
	case NVIF_VMM_V0_UNMAP : return nvkm_uvmm_mthd_unmap (uvmm, argv, argc);
	case NVIF_VMM_V0_PFNMAP: return nvkm_uvmm_mthd_pfnmap(uvmm, argv, argc);
	case NVIF_VMM_V0_PFNCLR: return nvkm_uvmm_mthd_pfnclr(uvmm, argv, argc);
	case NVIF_VMM_V0_RAW   : return nvkm_uvmm_mthd_raw   (uvmm, argv, argc);
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
