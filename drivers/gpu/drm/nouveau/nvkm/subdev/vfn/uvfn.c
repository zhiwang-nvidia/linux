/*
 * Copyright 2021 Red Hat Inc.
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
#include "uvfn.h"
#include "priv.h"

#include <core/object.h>

struct nvif_usermode_priv {
	struct nvkm_object object;
	struct nvkm_vfn *vfn;

	struct nvif_usermode_impl impl;
};

static void
nvkm_uvfn_del(struct nvif_usermode_priv *uvfn)
{
	struct nvkm_object *object = &uvfn->object;

	nvkm_object_del(&object);
}

static const struct nvif_usermode_impl
nvkm_uvfn_impl = {
	.del = nvkm_uvfn_del,
};

static const struct nvkm_object_func
nvkm_uvfn = {
};

int
nvkm_uvfn_new(struct nvkm_device *device, const struct nvif_usermode_impl **pimpl,
	      struct nvif_usermode_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_vfn *vfn = device->vfn;
	struct nvif_usermode_priv *uvfn;

	if (!(uvfn = kzalloc(sizeof(*uvfn), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uvfn, &(struct nvkm_oclass) {}, &uvfn->object);
	uvfn->vfn = device->vfn;

	uvfn->impl = nvkm_uvfn_impl;
	uvfn->impl.map.type = NVIF_MAP_IO;
	uvfn->impl.map.handle = device->func->resource_addr(device, 0) + vfn->addr.user;
	uvfn->impl.map.length = vfn->func->user.size;

	*pimpl = &uvfn->impl;
	*ppriv = uvfn;
	*pobject = &uvfn->object;
	return 0;
}
