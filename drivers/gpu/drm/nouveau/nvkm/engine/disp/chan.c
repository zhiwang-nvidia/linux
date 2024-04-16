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
#include "uchan.h"

#include <core/oproxy.h>
#include <core/ramht.h>
#include <subdev/mmu.h>

struct nvif_disp_chan_priv {
	struct nvkm_object object;
	struct nvkm_disp_chan chan;

	struct nvif_disp_chan_impl impl;
};

static int
nvkm_disp_chan_ntfy(struct nvkm_object *object, u32 type, struct nvkm_event **pevent)
{
	struct nvif_disp_chan_priv *uchan = container_of(object, typeof(*uchan), object);
	struct nvkm_disp *disp = uchan->chan.disp;

	switch (type) {
	case 0:
		*pevent = &disp->uevent;
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

struct nvif_ctxdma_priv {
	struct nvkm_oproxy oproxy;
	struct nvkm_disp *disp;
	int hash;
};

static void
nvkm_disp_chan_ctxdma_del(struct nvif_ctxdma_priv *ctxdma)
{
	struct nvkm_object *object = &ctxdma->oproxy.base;

	nvkm_object_del(&object);
}

static const struct nvif_ctxdma_impl
nvkm_disp_chan_ctxdma_impl = {
	.del = nvkm_disp_chan_ctxdma_del,
};

static void
nvkm_disp_chan_ctxdma_dtor(struct nvkm_oproxy *base)
{
	struct nvif_ctxdma_priv *object = container_of(base, typeof(*object), oproxy);

	nvkm_ramht_remove(object->disp->ramht, object->hash);
}

static const struct nvkm_oproxy_func
nvkm_disp_chan_ctxdma = {
	.dtor[0] = nvkm_disp_chan_ctxdma_dtor,
};

#include <engine/dma/priv.h>

static int
nvkm_disp_chan_ctxdma_new(struct nvif_disp_chan_priv *uchan, u32 handle, s32 oclass,
			  struct nv_dma_v0 *args, u32 argc,
			  const struct nvif_ctxdma_impl **pimpl, struct nvif_ctxdma_priv **ppriv)
{
	struct nvkm_disp_chan *chan = &uchan->chan;
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_dma *dma = device->dma;
	struct nvkm_dmaobj *dmaobj;
	struct nvif_ctxdma_priv *object;
	int ret;

	if (!(object = kzalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_oproxy_ctor(&nvkm_disp_chan_ctxdma, &(struct nvkm_oclass) {}, &object->oproxy);
	object->disp = disp;

	ret = dma->func->class_new(dma, &(struct nvkm_oclass) {
					.client = uchan->object.client,
					.base.oclass = oclass
				   }, args, argc, &dmaobj);
	object->oproxy.object = &dmaobj->object;
	if (ret)
		goto done;

	object->hash = chan->func->bind(chan, object->oproxy.object, handle);
	if (object->hash < 0) {
		ret = object->hash;
		goto done;
	}

	*pimpl = &nvkm_disp_chan_ctxdma_impl;
	*ppriv = object;

	nvkm_object_link(&uchan->object, &object->oproxy.base);

done:
	if (ret)
		nvkm_disp_chan_ctxdma_del(object);

	return ret;
}

static void
nvkm_disp_chan_del(struct nvif_disp_chan_priv *uchan)
{
	struct nvkm_object *object = &uchan->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_disp_chan_impl
nvkm_disp_chan_impl = {
	.del = nvkm_disp_chan_del,
};

static int
nvkm_disp_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nvif_disp_chan_priv *uchan = container_of(object, typeof(*uchan), object);
	struct nvkm_disp_chan *chan = &uchan->chan;

	chan->func->fini(chan);
	chan->func->intr(chan, false);
	return 0;
}

static int
nvkm_disp_chan_init(struct nvkm_object *object)
{
	struct nvif_disp_chan_priv *uchan = container_of(object, typeof(*uchan), object);
	struct nvkm_disp_chan *chan = &uchan->chan;

	chan->func->intr(chan, true);
	return chan->func->init(chan);
}

static void *
nvkm_disp_chan_dtor(struct nvkm_object *object)
{

	struct nvif_disp_chan_priv *uchan = container_of(object, typeof(*uchan), object);
	struct nvkm_disp_chan *chan = &uchan->chan;
	struct nvkm_disp *disp = chan->disp;

	spin_lock(&disp->user.lock);
	disp->chan[chan->chid.user] = NULL;
	spin_unlock(&disp->user.lock);

	nvkm_memory_unref(&chan->memory);
	return uchan;
}

static const struct nvkm_object_func
nvkm_disp_chan = {
	.dtor = nvkm_disp_chan_dtor,
	.init = nvkm_disp_chan_init,
	.fini = nvkm_disp_chan_fini,
	.ntfy = nvkm_disp_chan_ntfy,
};

int
nvkm_disp_chan_new(struct nvkm_disp *disp, const struct nvkm_disp_func_chan *func, u8 id,
		   struct nvkm_memory *memory, const struct nvif_disp_chan_impl **pimpl,
		   struct nvif_disp_chan_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvif_disp_chan_priv *uchan;
	struct nvkm_disp_chan *chan;
	int ret;

	if (!memory != !func->chan->func->push)
		return -EINVAL;

	uchan = kzalloc(sizeof(*uchan), GFP_KERNEL);
	if (!uchan)
		return -ENOMEM;
	chan = &uchan->chan;

	nvkm_object_ctor(&nvkm_disp_chan, &(struct nvkm_oclass) {}, &uchan->object);
	chan->func = func->chan->func;
	chan->mthd = func->chan->mthd;
	chan->disp = disp;
	chan->chid.ctrl = func->chan->ctrl + id;
	chan->chid.user = func->chan->user + id;
	chan->head = id;

	spin_lock(&disp->user.lock);
	if (disp->chan[chan->chid.user]) {
		spin_unlock(&disp->user.lock);
		kfree(uchan);
		return -EBUSY;
	}
	disp->chan[chan->chid.user] = chan;
	chan->user.oclass = func->oclass;
	spin_unlock(&disp->user.lock);

	*pobject = &uchan->object;

	uchan->impl = nvkm_disp_chan_impl;
	uchan->impl.map.type = NVIF_MAP_IO;
	uchan->impl.map.handle = device->func->resource_addr(device, 0);
	uchan->impl.map.handle += chan->func->user(chan, &uchan->impl.map.length);
	if (chan->func->bind)
		uchan->impl.ctxdma.new = nvkm_disp_chan_ctxdma_new;

	if (chan->func->push) {
		chan->memory = nvkm_memory_ref(memory);

		ret = chan->func->push(chan);
		if (ret) {
			nvkm_object_del(pobject);
			return ret;
		}
	}

	ret = nvkm_disp_chan_init(&uchan->object);
	if (ret)
		goto done;

	*pimpl = &uchan->impl;
	*ppriv = uchan;

done:
	if (ret)
		nvkm_disp_chan_del(uchan);

	return ret;
}
