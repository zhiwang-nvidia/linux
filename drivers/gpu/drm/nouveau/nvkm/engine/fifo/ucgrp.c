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
#include "ucgrp.h"
#include "cgrp.h"
#include "priv.h"
#include "runl.h"

#include <subdev/mmu/uvmm.h>

struct nvif_cgrp_priv {
	struct nvkm_object object;
	struct nvkm_cgrp *cgrp;

	struct nvif_cgrp_impl impl;
};

static int
nvkm_ucgrp_chan_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	struct nvkm_cgrp *cgrp = container_of(oclass->parent, struct nvif_cgrp_priv, object)->cgrp;

	return nvkm_uchan_new(cgrp->runl->fifo, cgrp, oclass, argv, argc, pobject);
}

static int
nvkm_ucgrp_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	struct nvkm_cgrp *cgrp = container_of(object, struct nvif_cgrp_priv, object)->cgrp;
	struct nvkm_fifo *fifo = cgrp->runl->fifo;
	const struct nvkm_fifo_func_chan *chan = &fifo->func->chan;
	int c = 0;

	/* *_CHANNEL_GPFIFO_* */
	if (chan->user.oclass) {
		if (c++ == index) {
			oclass->base = chan->user;
			oclass->ctor = nvkm_ucgrp_chan_new;
			return 0;
		}
	}

	return -EINVAL;
}

static void
nvkm_ucgrp_del(struct nvif_cgrp_priv *ucgrp)
{
	struct nvkm_object *object = &ucgrp->object;

	nvkm_object_del(&object);
}

static const struct nvif_cgrp_impl
nvkm_ucgrp_impl = {
	.del = nvkm_ucgrp_del,
};

static void *
nvkm_ucgrp_dtor(struct nvkm_object *object)
{
	struct nvif_cgrp_priv *ucgrp = container_of(object, typeof(*ucgrp), object);

	nvkm_cgrp_unref(&ucgrp->cgrp);
	return ucgrp;
}

static const struct nvkm_object_func
nvkm_ucgrp = {
	.dtor = nvkm_ucgrp_dtor,
	.sclass = nvkm_ucgrp_sclass,
};

int
nvkm_ucgrp_new(struct nvkm_fifo *fifo, u8 runi, struct nvif_vmm_priv *uvmm, const char *name,
	       const struct nvif_cgrp_impl **pimpl, struct nvif_cgrp_priv **ppriv,
	       struct nvkm_object **pobject)
{
	struct nvkm_runl *runl;
	struct nvkm_vmm *vmm;
	struct nvif_cgrp_priv *ucgrp;
	struct nvkm_engine *engine;
	int ret;

	/* Lookup objects referenced in args. */
	runl = nvkm_runl_get(fifo, runi, 0);
	if (!runl)
		return -EINVAL;

	vmm = nvkm_uvmm_ref(uvmm);
	if (!vmm)
		return -EINVAL;

	/* Allocate channel group. */
	if (!(ucgrp = kzalloc(sizeof(*ucgrp), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	engine = nvkm_engine_ref(&fifo->engine);
	if (IS_ERR(engine)) {
		ret = PTR_ERR(engine);
		goto done;
	}

	ret = nvkm_cgrp_new(runl, name, vmm, true, &ucgrp->cgrp);
	if (ret) {
		nvkm_engine_unref(&engine);
		goto done;
	}

	nvkm_object_ctor(&nvkm_ucgrp, &(struct nvkm_oclass) { .engine = engine }, &ucgrp->object);

	/* Return channel group info to caller. */
	ucgrp->impl = nvkm_ucgrp_impl;
	ucgrp->impl.id = ucgrp->cgrp->id;

	*pimpl = &ucgrp->impl;
	*ppriv = ucgrp;
	*pobject = &ucgrp->object;

done:
	if (ret)
		kfree(ucgrp);

	nvkm_vmm_unref(&vmm);
	return ret;
}
