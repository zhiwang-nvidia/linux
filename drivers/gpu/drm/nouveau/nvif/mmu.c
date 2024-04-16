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

void
nvif_mmu_dtor(struct nvif_mmu *mmu)
{
	if (!mmu->impl)
		return;

	mmu->impl->del(mmu->priv);
	mmu->impl = NULL;
}

int
nvif_mmu_ctor(struct nvif_device *device, const char *name, struct nvif_mmu *mmu)
{
	const s32 oclass = device->impl->mmu.oclass;
	int ret;

	mmu->impl = NULL;

	ret = device->impl->mmu.new(device->priv, &mmu->impl, &mmu->priv,
				    nvif_handle(&mmu->object));
	NVIF_ERRON(ret, &device->object, "[NEW mmu%08x]", oclass);
	if (ret)
		return ret;

	nvif_object_ctor(&device->object, name ?: "nvifMmu", 0, oclass, &mmu->object);
	return 0;
}
