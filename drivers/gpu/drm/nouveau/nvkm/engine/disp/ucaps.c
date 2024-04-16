/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "ucaps.h"

struct nvif_disp_caps_priv {
	struct nvkm_object object;
	struct nvkm_disp *disp;

	struct nvif_disp_caps_impl impl;
};

static void
nvkm_ucaps_del(struct nvif_disp_caps_priv *ucaps)
{
	struct nvkm_object *object = &ucaps->object;

	nvkm_object_del(&object);
}

static const struct nvif_disp_caps_impl
nvkm_ucaps_impl = {
	.del = nvkm_ucaps_del,
};

static const struct nvkm_object_func
nvkm_ucaps = {
};

int
nvkm_ucaps_new(struct nvkm_disp *disp, const struct nvif_disp_caps_impl **pimpl,
	       struct nvif_disp_caps_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvif_disp_caps_priv *ucaps;

	ucaps = kzalloc(sizeof(*ucaps), GFP_KERNEL);
	if (!ucaps)
		return -ENOMEM;
	*pobject = &ucaps->object;

	nvkm_object_ctor(&nvkm_ucaps, &(struct nvkm_oclass) {}, &ucaps->object);
	ucaps->disp = disp;
	ucaps->impl = nvkm_ucaps_impl;

	ucaps->impl.map.type = NVIF_MAP_IO;
	disp->func->user.caps.addr(disp, &ucaps->impl.map.handle, &ucaps->impl.map.length);

	*pimpl = &ucaps->impl;
	*ppriv = ucaps;
	*pobject = &ucaps->object;
	return 0;
}
