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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"
#include "uchan.h"

#include <core/gpuobj.h>
#include <core/oproxy.h>
#include <subdev/mmu.h>
#include <subdev/mmu/umem.h>
#include <engine/dma.h>

struct nvif_chan_priv {
	struct nvkm_object object;
	struct nvkm_chan *chan;

	struct nvif_chan_impl impl;
};

struct nvkm_uobj {
	struct nvkm_oproxy oproxy;
	struct nvkm_chan *chan;
	struct nvkm_cctx *cctx;
	int hash;
};

static void
nvkm_uchan_object_del(struct nvif_engobj_priv *priv)
{
	struct nvkm_uobj *uobj = (void *)priv;
	struct nvkm_object *object = &uobj->oproxy.base;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_engobj_impl
nvkm_uchan_object_impl = {
	.del = nvkm_uchan_object_del,
};

static int
nvkm_uchan_object_fini_1(struct nvkm_oproxy *oproxy, bool suspend)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_chan *chan = uobj->chan;
	struct nvkm_cctx *cctx = uobj->cctx;
	struct nvkm_ectx *ectx = cctx->vctx->ectx;

	if (!ectx->object)
		return 0;

	/* Unbind engine context from channel, if no longer required. */
	if (refcount_dec_and_mutex_lock(&cctx->uses, &chan->cgrp->mutex)) {
		nvkm_chan_cctx_bind(chan, ectx->engn, NULL);

		if (refcount_dec_and_test(&ectx->uses))
			nvkm_object_fini(ectx->object, false);
		mutex_unlock(&chan->cgrp->mutex);
	}

	return 0;
}

static int
nvkm_uchan_object_init_0(struct nvkm_oproxy *oproxy)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_chan *chan = uobj->chan;
	struct nvkm_cctx *cctx = uobj->cctx;
	struct nvkm_ectx *ectx = cctx->vctx->ectx;
	int ret = 0;

	if (!ectx->object)
		return 0;

	/* Bind engine context to channel, if it hasn't been already. */
	if (!refcount_inc_not_zero(&cctx->uses)) {
		mutex_lock(&chan->cgrp->mutex);
		if (!refcount_inc_not_zero(&cctx->uses)) {
			if (!refcount_inc_not_zero(&ectx->uses)) {
				ret = nvkm_object_init(ectx->object);
				if (ret == 0)
					refcount_set(&ectx->uses, 1);
			}

			if (ret == 0) {
				nvkm_chan_cctx_bind(chan, ectx->engn, cctx);
				refcount_set(&cctx->uses, 1);
			}
		}
		mutex_unlock(&chan->cgrp->mutex);
	}

	return ret;
}

static void
nvkm_uchan_object_dtor(struct nvkm_oproxy *oproxy)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_engn *engn;

	if (!uobj->cctx)
		return;

	engn = uobj->cctx->vctx->ectx->engn;
	if (engn->func->ramht_del)
		engn->func->ramht_del(uobj->chan, uobj->hash);

	nvkm_chan_cctx_put(uobj->chan, &uobj->cctx);
}

static const struct nvkm_oproxy_func
nvkm_uchan_object = {
	.dtor[1] = nvkm_uchan_object_dtor,
	.init[0] = nvkm_uchan_object_init_0,
	.fini[1] = nvkm_uchan_object_fini_1,
};

static int
nvkm_uchan_object_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		      struct nvkm_object **pobject)
{
	struct nvkm_chan *chan = container_of(oclass->parent, struct nvif_chan_priv, object)->chan;
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_engn *engn;
	struct nvkm_uobj *uobj;
	int ret;

	/* Lookup host engine state for target engine. */
	engn = nvkm_runl_find_engn(engn, cgrp->runl, engn->engine == oclass->engine);
	if (WARN_ON(!engn))
		return -EINVAL;

	/* Allocate SW object. */
	if (!(uobj = kzalloc(sizeof(*uobj), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_oproxy_ctor(&nvkm_uchan_object, oclass, &uobj->oproxy);
	uobj->chan = chan;
	*pobject = &uobj->oproxy.base;

	/* Ref. channel context for target engine.*/
	ret = nvkm_chan_cctx_get(chan, engn, &uobj->cctx, oclass->client);
	if (ret)
		return ret;

	/* Allocate HW object. */
	ret = oclass->base.ctor(&(const struct nvkm_oclass) {
					.base = oclass->base,
					.engn = oclass->engn,
					.handle = oclass->handle,
					.object = oclass->object,
					.client = oclass->client,
					.parent = uobj->cctx->vctx->ectx->object ?: oclass->parent,
					.engine = engn->engine,
				 }, NULL, 0, &uobj->oproxy.object);
	if (ret)
		return ret;

	if (engn->func->ramht_add) {
		uobj->hash = engn->func->ramht_add(engn, uobj->oproxy.object, uobj->chan);
		if (uobj->hash < 0)
			return uobj->hash;
	}

	return 0;
}

static int
nvkm_uchan_engobj_new(struct nvif_chan_priv *uchan, u32 handle, u8 engi, s32 oclass,
		      const struct nvif_engobj_impl **pimpl, struct nvif_engobj_priv **ppriv,
		      u64 _handle)
{
	struct nvkm_chan *chan = uchan->chan;
	struct nvkm_runl *runl = chan->cgrp->runl;
	struct nvkm_engn *engt, *engn = NULL;
	struct nvkm_engine *engine;
	struct nvkm_oclass _oclass = {};
	struct nvkm_object *object = NULL;
	int ret, i = 0;

	nvkm_runl_foreach_engn(engt, runl) {
		if (i++ == engi) {
			engn = engt;
			break;
		}
	}

	if (!engn)
		return -EINVAL;

	engine = engn->engine;

	_oclass.handle = handle;
	_oclass.client = uchan->object.client;
	_oclass.parent = &uchan->object;
	_oclass.engine = engine;

	if (engine->func->fifo.sclass) {
		i = 0;
		do {
			_oclass.base.oclass = 0;
			engine->func->fifo.sclass(&_oclass, i++);
			if (_oclass.base.oclass == oclass)
				break;
		} while (_oclass.base.oclass);
	} else {
		for (i = 0; engine->func->sclass[i].oclass; i++) {
			if (engine->func->sclass[i].oclass == oclass) {
				_oclass.base = engine->func->sclass[i];
				break;
			}
		}
	}

	if (!_oclass.base.oclass)
		return -EINVAL;

	if (!_oclass.base.ctor)
		_oclass.base.ctor = nvkm_object_new;

	if (engine) {
		_oclass.engine = nvkm_engine_ref(engine);
		if (IS_ERR(_oclass.engine))
			return PTR_ERR(_oclass.engine);
	}

	ret = nvkm_uchan_object_new(&_oclass, NULL, 0, &object);
	nvkm_engine_unref(&_oclass.engine);
	if (ret)
		goto done;

	ret = nvkm_object_init(object);
	if (ret) {
		nvkm_object_fini(object, false);
		goto done;
	}

	*pimpl = &nvkm_uchan_object_impl;
	*ppriv = (void *)container_of(object, struct nvkm_uobj, oproxy.base);

	ret = nvkm_object_link_rb(uchan->object.client, &uchan->object, _handle, object);
	if (ret)
		nvkm_object_fini(object, false);

done:
	if (ret)
		nvkm_object_del(&object);

	return ret;
}

static void
nvkm_uchan_ctxdma_del(struct nvif_ctxdma_priv *priv)
{
	struct nvkm_uobj *uobj = (void *)priv;
	struct nvkm_object *object = &uobj->oproxy.base;

	nvkm_object_del(&object);
}

static const struct nvif_ctxdma_impl
nvkm_uchan_ctxdma_impl = {
	.del = nvkm_uchan_ctxdma_del,
};

static int
nvkm_uchan_ctxdma_new(struct nvif_chan_priv *uchan, u32 handle, s32 oclass, void *argv, u32 argc,
		      const struct nvif_ctxdma_impl **pimpl, struct nvif_ctxdma_priv **ppriv)
{
	struct nvkm_dma *dma = uchan->chan->cgrp->runl->fifo->engine.subdev.device->dma;
	struct nvkm_oclass _oclass = {};
	struct nvkm_object *object;
	int i, ret;

	_oclass.client = uchan->object.client;
	_oclass.parent = &uchan->object;
	_oclass.engine = &dma->engine;
	_oclass.handle = handle;

	i = 0;
	do {
		_oclass.base.oclass = 0;
		dma->engine.func->fifo.sclass(&_oclass, i++);
		if (_oclass.base.oclass == oclass)
			break;
	} while (_oclass.base.oclass);

	if (!_oclass.base.oclass)
		return -EINVAL;

	ret = nvkm_uchan_object_new(&_oclass, argv, argc, &object);
	if (ret)
		return ret;

	*pimpl = &nvkm_uchan_ctxdma_impl;
	*ppriv = (void *)container_of(object, struct nvkm_uobj, oproxy.base);

	nvkm_object_link(&uchan->object, object);
	return 0;
}

static int
nvkm_uchan_event_nonstall(struct nvif_chan_priv *uchan, u64 token,
			  const struct nvif_event_impl **pimpl, struct nvif_event_priv **ppriv)
{
	struct nvkm_runl *runl = uchan->chan->cgrp->runl;

	return nvkm_uevent_new_(&uchan->object, token, &runl->fifo->nonstall.event, false,
				runl->id, NVKM_FIFO_NONSTALL_EVENT, NULL, pimpl, ppriv);
}

static int
nvkm_uchan_event_killed(struct nvif_chan_priv *uchan, u64 token,
			const struct nvif_event_impl **pimpl, struct nvif_event_priv **ppriv)
{
	struct nvkm_runl *runl = uchan->chan->cgrp->runl;

	return nvkm_uevent_new_(&uchan->object, token, &runl->chid->event, false,
				uchan->chan->id, NVKM_CHAN_EVENT_ERRORED, NULL, pimpl, ppriv);
}

static void
nvkm_uchan_del(struct nvif_chan_priv *uchan)
{
	struct nvkm_object *object = &uchan->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_chan_impl
nvkm_uchan_impl = {
	.del = nvkm_uchan_del,
	.event.killed = nvkm_uchan_event_killed,
	.ctxdma.new = nvkm_uchan_ctxdma_new,
	.engobj.new = nvkm_uchan_engobj_new,
};

static int
nvkm_uchan_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_chan *chan = container_of(object, struct nvif_chan_priv, object)->chan;

	nvkm_chan_block(chan);
	nvkm_chan_remove(chan, true);

	if (chan->func->unbind)
		chan->func->unbind(chan);

	return 0;
}

static int
nvkm_uchan_init(struct nvkm_object *object)
{
	struct nvkm_chan *chan = container_of(object, struct nvif_chan_priv, object)->chan;

	if (atomic_read(&chan->errored))
		return 0;

	if (chan->func->bind)
		chan->func->bind(chan);

	nvkm_chan_allow(chan);
	nvkm_chan_insert(chan);
	return 0;
}

static void *
nvkm_uchan_dtor(struct nvkm_object *object)
{
	struct nvif_chan_priv *uchan = container_of(object, typeof(*uchan), object);

	nvkm_chan_del(&uchan->chan);
	return uchan;
}

static const struct nvkm_object_func
nvkm_uchan = {
	.dtor = nvkm_uchan_dtor,
	.init = nvkm_uchan_init,
	.fini = nvkm_uchan_fini,
};

struct nvkm_chan *
nvkm_uchan_chan(struct nvkm_object *object)
{
	if (WARN_ON(object->func != &nvkm_uchan))
		return NULL;

	return container_of(object, struct nvif_chan_priv, object)->chan;
}

int
nvkm_uchan_new(struct nvkm_device *device, struct nvkm_cgrp *cgrp, u8 runi, u8 runq, bool priv,
	       u16 devm, struct nvkm_vmm *vmm, struct nvif_ctxdma_priv *upush, u64 offset,
	       u64 length, struct nvif_mem_priv *uuserd, u16 userd_offset, const char *name,
	       const struct nvif_chan_impl **pimpl, struct nvif_chan_priv **ppriv,
	       struct nvkm_object **pobject)
{
	struct nvkm_fifo *fifo = device->fifo;
	struct nvkm_runl *runl;
	struct nvkm_dmaobj *ctxdma = (void *)upush;
	struct nvkm_memory *userd = NULL;
	struct nvif_chan_priv *uchan;
	struct nvkm_engine *engine;
	struct nvkm_chan *chan;
	int ret;

	/* Lookup objects referenced in args. */
	runl = nvkm_runl_get(fifo, runi, 0);
	if (!runl)
		return -EINVAL;

	ctxdma = (void *)upush;
	userd = nvkm_umem_ref(uuserd);

	/* Allocate channel. */
	uchan = kzalloc(sizeof(*uchan), GFP_KERNEL);
	if (!uchan) {
		nvkm_memory_unref(&userd);
		return -ENOMEM;
	}

	engine = nvkm_engine_ref(&fifo->engine);
	if (IS_ERR(engine)) {
		kfree(uchan);
		nvkm_memory_unref(&userd);
		return PTR_ERR(engine);
	}

	nvkm_object_ctor(&nvkm_uchan, &(struct nvkm_oclass) { .engine = engine }, &uchan->object);

	ret = nvkm_chan_new_(fifo->func->chan.func, runl, runq, cgrp, name, priv, devm,
			     vmm, ctxdma, offset, length, userd, userd_offset,
			     &uchan->chan);
	if (ret)
		goto done;

	ret = nvkm_uchan_init(&uchan->object);
	if (ret)
		goto done;

	chan = uchan->chan;

	/* Return channel info to caller. */
	uchan->impl = nvkm_uchan_impl;
	uchan->impl.id = chan->id;
	if (chan->func->doorbell_handle)
		uchan->impl.doorbell_token = chan->func->doorbell_handle(chan);

	switch (nvkm_memory_target(chan->inst->memory)) {
	case NVKM_MEM_TARGET_INST: uchan->impl.inst.aper = NVIF_CHAN_INST_APER_INST; break;
	case NVKM_MEM_TARGET_VRAM: uchan->impl.inst.aper = NVIF_CHAN_INST_APER_VRAM; break;
	case NVKM_MEM_TARGET_HOST: uchan->impl.inst.aper = NVIF_CHAN_INST_APER_HOST; break;
	case NVKM_MEM_TARGET_NCOH: uchan->impl.inst.aper = NVIF_CHAN_INST_APER_NCOH; break;
	default:
		WARN_ON(1);
		ret = -EFAULT;
		goto done;
	}

	uchan->impl.inst.addr = nvkm_memory_addr(chan->inst->memory);

	if (chan->func->userd->bar >= 0) {
		uchan->impl.map.type = NVIF_MAP_IO;
		uchan->impl.map.handle =
			device->func->resource_addr(device, chan->func->userd->bar) +
			chan->func->userd->base + chan->userd.base;
		uchan->impl.map.length = chan->func->userd->size;
	}

	if (fifo->func->nonstall)
		uchan->impl.event.nonstall = nvkm_uchan_event_nonstall;

	*pimpl = &uchan->impl;
	*ppriv = uchan;
	*pobject = &uchan->object;

done:
	if (ret)
		nvkm_uchan_del(uchan);

	nvkm_memory_unref(&userd);
	return ret;
}
