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
#include <engine/fifo/uchan.h>

struct nvif_cgrp_priv {
	struct nvkm_object object;
	struct nvkm_cgrp *cgrp;

	struct nvif_cgrp_impl impl;
};

static int
nvkm_ucgrp_chan_new(struct nvif_cgrp_priv *ucgrp, u8 runq, bool priv, u16 devm,
		    u64 gpfifo_offset, u64 gpfifo_length,
		    struct nvif_mem_priv *userd, u16 userd_offset, const char *name,
		    const struct nvif_chan_impl **pimpl, struct nvif_chan_priv **ppriv)
{
	struct nvkm_cgrp *cgrp = ucgrp->cgrp;
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uchan_new(cgrp->runl->fifo->engine.subdev.device, cgrp, cgrp->runl->id,
			     runq, priv, devm, cgrp->vmm, NULL, gpfifo_offset, gpfifo_length,
			     userd, userd_offset, name, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&ucgrp->object, object);
	return 0;
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
	.chan.new = nvkm_ucgrp_chan_new,
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
