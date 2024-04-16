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
#include <nvif/vmm.h>
#include <nvif/driverif.h>
#include <nvif/mem.h>
#include <nvif/printf.h>

int
nvif_vmm_unmap(struct nvif_vmm *vmm, u64 addr)
{
	return vmm->impl->unmap(vmm->priv, addr);
}

int
nvif_vmm_map(struct nvif_vmm *vmm, u64 addr, u64 size, void *argv, u32 argc,
	     struct nvif_mem *mem, u64 offset)
{
	return vmm->impl->map(vmm->priv, addr, size, argv, argc, mem->priv, offset);
}

void
nvif_vmm_put(struct nvif_vmm *vmm, struct nvif_vma *vma)
{
	if (vmm && vma->size) {
		WARN_ON(vmm->impl->put(vmm->priv, vma->addr));
		vma->size = 0;
	}
}

int
nvif_vmm_get(struct nvif_vmm *vmm, enum nvif_vmm_get_type type, bool sparse,
	     u8 page, u8 align, u64 size, struct nvif_vma *vma)
{
	int ret;

	vma->size = 0;

	ret = vmm->impl->get(vmm->priv, type, sparse, page, align, size, &vma->addr);
	if (ret)
		return ret;

	vma->size = size;
	return 0;
}

int
nvif_vmm_raw_get(struct nvif_vmm *vmm, u64 addr, u64 size,
		 u8 shift)
{
	return vmm->impl->raw.get(vmm->priv, shift, addr, size);
}

int
nvif_vmm_raw_put(struct nvif_vmm *vmm, u64 addr, u64 size, u8 shift)
{
	return vmm->impl->raw.put(vmm->priv, shift, addr, size);
}

int
nvif_vmm_raw_map(struct nvif_vmm *vmm, u64 addr, u64 size, u8 shift,
		 void *argv, u32 argc, struct nvif_mem *mem, u64 offset)
{
	return vmm->impl->raw.map(vmm->priv, shift, addr, size, argv, argc, mem->priv, offset);
}

int
nvif_vmm_raw_unmap(struct nvif_vmm *vmm, u64 addr, u64 size,
		   u8 shift, bool sparse)
{
	return vmm->impl->raw.unmap(vmm->priv, shift, addr, size, sparse);
}

int
nvif_vmm_raw_sparse(struct nvif_vmm *vmm, u64 addr, u64 size, bool ref)
{
	return vmm->impl->raw.sparse(vmm->priv, addr, size, ref);
}

void
nvif_vmm_dtor(struct nvif_vmm *vmm)
{
	if (!vmm->impl)
		return;

	vmm->impl->del(vmm->priv);
	vmm->impl = NULL;
}

int
nvif_vmm_ctor(struct nvif_mmu *mmu, const char *name,
	      enum nvif_vmm_type type, u64 addr, u64 size, void *argv, u32 argc,
	      struct nvif_vmm *vmm)
{
	const u32 oclass = mmu->impl->vmm.oclass;
	int ret;

	ret = mmu->impl->vmm.new(mmu->priv, type, addr, size, argv, argc, &vmm->impl, &vmm->priv,
				 nvif_handle(&vmm->object));
	NVIF_ERRON(ret, &mmu->object, "[NEW vmm%08x]", oclass);
	if (ret)
		return ret;

	nvif_object_ctor(&mmu->object, name ?: "nvifVmm", 0, oclass, &vmm->object);
	return ret;
}
