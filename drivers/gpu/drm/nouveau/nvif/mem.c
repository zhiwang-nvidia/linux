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
#include <nvif/mem.h>
#include <nvif/client.h>
#include <nvif/driverif.h>
#include <nvif/printf.h>

#include <nvif/if000a.h>

int
nvif_mem_unmap(struct nvif_mem *mem)
{
	if (!mem->mapinfo.length)
		return 0;

	mem->impl->unmap(mem->priv);
	mem->mapinfo.length = 0;
	return 0;
}

int
nvif_mem_map(struct nvif_mem *mem, void *argv, u32 argc, struct nvif_map *map)
{
	int ret;

	ret = mem->impl->map(mem->priv, argv, argc, &mem->mapinfo);
	if (ret)
		return ret;

	ret = nvif_object_map_cpu(&mem->object, &mem->mapinfo, map);
	if (ret)
		mem->impl->unmap(mem->priv);

	return ret;
}

int
nvif_mem_unmap_dtor(struct nvif_mem *mem, struct nvif_map *map)
{
	int ret = 0;

	if (mem->mapinfo.length) {
		if (map)
			ret = nvif_object_unmap_cpu(map);

		nvif_mem_unmap(mem);
	}

	return ret;
}

int
nvif_mem_ctor_map(struct nvif_mmu *mmu, const char *name, u8 type, u64 size,
		  struct nvif_mem *mem, struct nvif_map *map)
{
	int ret = nvif_mem_ctor(mmu, name, NVIF_MEM_MAPPABLE | type,
				0, size, NULL, 0, mem);
	if (ret == 0) {
		ret = nvif_mem_map(mem, NULL, 0, map);
		if (ret)
			nvif_mem_dtor(mem);
	}
	return ret;
}

void
nvif_mem_dtor(struct nvif_mem *mem)
{
	if (!mem->impl)
		return;

	nvif_mem_unmap(mem);

	mem->impl->del(mem->priv);
	mem->impl = NULL;
}

int
nvif_mem_ctor_type(struct nvif_mmu *mmu, const char *name,
		   int type, u8 page, u64 size, void *argv, u32 argc,
		   struct nvif_mem *mem)
{
	const u32 oclass = mmu->impl->mem.oclass;
	int ret;

	if (type < 0)
		return -EINVAL;

	ret = mmu->impl->mem.new(mmu->priv, type, page, size, argv, argc, &mem->impl, &mem->priv);
	NVIF_DEBUG(&mmu->object, "[NEW mem%08x] (ret:%d)", oclass, ret);
	if (ret)
		return ret;

	nvif_object_ctor(&mmu->object, name ?: "nvifMem", 0, mmu->impl->mem.oclass, &mem->object);
	mem->type = mmu->impl->type[type].type;
	return 0;

}

int
nvif_mem_ctor(struct nvif_mmu *mmu, const char *name, u8 type,
	      u8 page, u64 size, void *argv, u32 argc, struct nvif_mem *mem)
{
	int ret = -EINVAL, i;

	mem->object.client = NULL;

	for (i = 0; ret && i < mmu->impl->type_nr; i++) {
		if ((mmu->impl->type[i].type & type) == type) {
			ret = nvif_mem_ctor_type(mmu, name, i, page,
						 size, argv, argc, mem);
		}
	}

	return ret;
}
