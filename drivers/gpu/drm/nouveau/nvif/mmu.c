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
#include <nvif/mmu.h>
#include <nvif/device.h>
#include <nvif/printf.h>

#include <nvif/class.h>
#include <nvif/if0008.h>

void
nvif_mmu_dtor(struct nvif_mmu *mmu)
{
	if (!mmu->impl)
		return;

	kfree(mmu->kind);
	kfree(mmu->type);
	kfree(mmu->heap);
	mmu->impl->del(mmu->priv);
	mmu->impl = NULL;
}

int
nvif_mmu_ctor(struct nvif_device *device, const char *name, struct nvif_mmu *mmu)
{
	const s32 oclass = device->impl->mmu.oclass;
	int ret, i;

	mmu->impl = NULL;
	mmu->heap = NULL;
	mmu->type = NULL;
	mmu->kind = NULL;

	ret = device->impl->mmu.new(device->priv, &mmu->impl, &mmu->priv,
				    nvif_handle(&mmu->object));
	NVIF_ERRON(ret, &device->object, "[NEW mmu%08x]", oclass);
	if (ret)
		return ret;

	nvif_object_ctor(&device->object, name ?: "nvifMmu", 0, oclass, &mmu->object);

	mmu->heap_nr = mmu->impl->heap_nr;
	mmu->type_nr = mmu->impl->type_nr;
	mmu->kind_nr = mmu->impl->kind_nr;

	mmu->heap = kmalloc_array(mmu->heap_nr, sizeof(*mmu->heap),
				  GFP_KERNEL);
	mmu->type = kmalloc_array(mmu->type_nr, sizeof(*mmu->type),
				  GFP_KERNEL);
	if (ret = -ENOMEM, !mmu->heap || !mmu->type)
		goto done;

	mmu->kind = kmalloc_array(mmu->kind_nr, sizeof(*mmu->kind),
				  GFP_KERNEL);
	if (!mmu->kind && mmu->kind_nr)
		goto done;

	for (i = 0; i < mmu->heap_nr; i++) {
		struct nvif_mmu_heap_v0 args = { .index = i };

		ret = nvif_object_mthd(&mmu->object, NVIF_MMU_V0_HEAP,
				       &args, sizeof(args));
		if (ret)
			goto done;

		mmu->heap[i].size = args.size;
	}

	for (i = 0; i < mmu->type_nr; i++) {
		struct nvif_mmu_type_v0 args = { .index = i };

		ret = nvif_object_mthd(&mmu->object, NVIF_MMU_V0_TYPE,
				       &args, sizeof(args));
		if (ret)
			goto done;

		mmu->type[i].type = 0;
		if (args.vram) mmu->type[i].type |= NVIF_MEM_VRAM;
		if (args.host) mmu->type[i].type |= NVIF_MEM_HOST;
		if (args.comp) mmu->type[i].type |= NVIF_MEM_COMP;
		if (args.disp) mmu->type[i].type |= NVIF_MEM_DISP;
		if (args.kind    ) mmu->type[i].type |= NVIF_MEM_KIND;
		if (args.mappable) mmu->type[i].type |= NVIF_MEM_MAPPABLE;
		if (args.coherent) mmu->type[i].type |= NVIF_MEM_COHERENT;
		if (args.uncached) mmu->type[i].type |= NVIF_MEM_UNCACHED;
		mmu->type[i].heap = args.heap;
	}

	if (mmu->kind_nr) {
		struct nvif_mmu_kind_v0 *kind;
		size_t argc = struct_size(kind, data, mmu->kind_nr);

		if (ret = -ENOMEM, !(kind = kmalloc(argc, GFP_KERNEL)))
			goto done;
		kind->version = 0;
		kind->count = mmu->kind_nr;

		ret = nvif_object_mthd(&mmu->object, NVIF_MMU_V0_KIND,
				       kind, argc);
		if (ret == 0)
			memcpy(mmu->kind, kind->data, kind->count);
		mmu->kind_inv = kind->kind_inv;
		kfree(kind);
	}

done:
	if (ret)
		nvif_mmu_dtor(mmu);
	return ret;
}
