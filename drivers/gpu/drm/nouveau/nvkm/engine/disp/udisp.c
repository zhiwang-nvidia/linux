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
#include "udisp.h"
#include "ucaps.h"
#include "uchan.h"
#include "uconn.h"
#include "uhead.h"
#include "uoutp.h"
#include <subdev/mmu/umem.h>

struct nvif_disp_priv {
	struct nvkm_object object;
	struct nvkm_disp *disp;

	struct nvif_disp_impl impl;
};

static int
nvkm_udisp_chan_new(struct nvif_disp_priv *udisp, const struct nvkm_disp_func_chan *func,
		    u8 nr, u8 id, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_memory *memory = NULL;
	struct nvkm_object *object;
	int ret;

	if (id >= nr)
		return -EINVAL;

	if (umem)
		memory = nvkm_umem_ref(umem);

	ret = nvkm_disp_chan_new(udisp->disp, func, id, memory, pimpl, ppriv, &object);
	nvkm_memory_unref(&memory);
	if (ret)
		return ret;

	if (handle)
		return nvkm_object_link_rb(udisp->object.client, &udisp->object, handle, object);

	nvkm_object_link(&udisp->object, object);
	return 0;
}

static int
nvkm_udisp_oimm_new(struct nvif_disp_priv *udisp, u8 id,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.oimm, disp->head.nr, id, NULL,
				   pimpl, ppriv, 0);
}

static int
nvkm_udisp_curs_new(struct nvif_disp_priv *udisp, u8 id,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.curs, disp->head.nr, id, NULL,
				   pimpl, ppriv, 0);
}

static int
nvkm_udisp_wimm_new(struct nvif_disp_priv *udisp, u8 id, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.wimm, disp->wndw.nr, id, umem,
				   pimpl, ppriv, 0);
}

static int
nvkm_udisp_wndw_new(struct nvif_disp_priv *udisp, u8 id, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.wndw, disp->wndw.nr, id, umem,
				   pimpl, ppriv, handle);
}

static int
nvkm_udisp_ovly_new(struct nvif_disp_priv *udisp, u8 id, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.ovly, disp->head.nr, id, umem,
				   pimpl, ppriv, handle);
}

static int
nvkm_udisp_base_new(struct nvif_disp_priv *udisp, u8 id, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.base, disp->head.nr, id, umem,
				   pimpl, ppriv, handle);
}

static int
nvkm_udisp_core_new(struct nvif_disp_priv *udisp, struct nvif_mem_priv *umem,
		    const struct nvif_disp_chan_impl **pimpl, struct nvif_disp_chan_priv **ppriv,
		    u64 handle)
{
	struct nvkm_disp *disp = udisp->disp;

	return nvkm_udisp_chan_new(udisp, &disp->func->user.core, 1, 0, umem, pimpl, ppriv, handle);
}

static int
nvkm_udisp_head_new(struct nvif_disp_priv *udisp, u8 id,
		    const struct nvif_head_impl **pimpl, struct nvif_head_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uhead_new(udisp->disp, id, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udisp->object, object);
	return 0;
}

static int
nvkm_udisp_outp_new(struct nvif_disp_priv *udisp, u8 id,
		    const struct nvif_outp_impl **pimpl, struct nvif_outp_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uoutp_new(udisp->disp, id, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udisp->object, object);
	return 0;
}

static int
nvkm_udisp_conn_new(struct nvif_disp_priv *udisp, u8 id,
		    const struct nvif_conn_impl **pimpl, struct nvif_conn_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uconn_new(udisp->disp, id, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udisp->object, object);
	return 0;
}

static int
nvkm_udisp_caps_new(struct nvif_disp_priv *udisp,
		    const struct nvif_disp_caps_impl **pimpl, struct nvif_disp_caps_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_ucaps_new(udisp->disp, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udisp->object, object);
	return 0;
}

static void
nvkm_udisp_del(struct nvif_disp_priv *udisp)
{
	struct nvkm_object *object = &udisp->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_disp_impl
nvkm_udisp_impl = {
	.del = nvkm_udisp_del,
	.conn.new = nvkm_udisp_conn_new,
	.outp.new = nvkm_udisp_outp_new,
	.head.new = nvkm_udisp_head_new,
};

static void *
nvkm_udisp_dtor(struct nvkm_object *object)
{
	struct nvif_disp_priv *udisp = container_of(object, typeof(*udisp), object);
	struct nvkm_disp *disp = udisp->disp;
	struct nvkm_engine *engine = &disp->engine;

	spin_lock(&disp->user.lock);
	disp->user.allocated = false;
	spin_unlock(&disp->user.lock);

	nvkm_engine_unref(&engine);
	return udisp;
}

static const struct nvkm_object_func
nvkm_udisp = {
	.dtor = nvkm_udisp_dtor,
};

int
nvkm_udisp_new(struct nvkm_device *device, const struct nvif_disp_impl **pimpl,
	       struct nvif_disp_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = device->disp;
	struct nvkm_conn *conn;
	struct nvkm_outp *outp;
	struct nvkm_head *head;
	struct nvif_disp_priv *udisp;
	struct nvkm_engine *engine;

	udisp = kzalloc(sizeof(*udisp), GFP_KERNEL);
	if (!udisp)
		return -ENOMEM;

	engine = nvkm_engine_ref(&disp->engine);
	if (IS_ERR(engine)) {
		kfree(udisp);
		return PTR_ERR(engine);
	}

	spin_lock(&disp->user.lock);
	if (disp->user.allocated) {
		spin_unlock(&disp->user.lock);
		nvkm_engine_unref(&engine);
		kfree(udisp);
		return -EBUSY;
	}
	disp->user.allocated = true;
	spin_unlock(&disp->user.lock);

	nvkm_object_ctor(&nvkm_udisp, &(struct nvkm_oclass) {}, &udisp->object);
	udisp->disp = disp;
	udisp->impl = nvkm_udisp_impl;

	if (disp->func->user.caps.oclass) {
		udisp->impl.caps.oclass = disp->func->user.caps.oclass;
		udisp->impl.caps.new = nvkm_udisp_caps_new;
	}

	list_for_each_entry(conn, &disp->conns, head)
		udisp->impl.conn.mask |= BIT(conn->index);

	list_for_each_entry(outp, &disp->outps, head)
		udisp->impl.outp.mask |= BIT(outp->index);

	list_for_each_entry(head, &disp->heads, head)
		udisp->impl.head.mask |= BIT(head->id);

	if (disp->func->user.core.oclass) {
		udisp->impl.chan.core.oclass = disp->func->user.core.oclass;
		udisp->impl.chan.core.new = nvkm_udisp_core_new;
		udisp->impl.chan.curs.oclass = disp->func->user.curs.oclass;
		udisp->impl.chan.curs.new = nvkm_udisp_curs_new;

		if (!disp->func->user.wndw.oclass) {
			/* EVO */
			udisp->impl.chan.base.oclass = disp->func->user.base.oclass;
			udisp->impl.chan.base.new = nvkm_udisp_base_new;
			udisp->impl.chan.ovly.oclass = disp->func->user.ovly.oclass;
			udisp->impl.chan.ovly.new = nvkm_udisp_ovly_new;
			udisp->impl.chan.oimm.oclass = disp->func->user.oimm.oclass;
			udisp->impl.chan.oimm.new = nvkm_udisp_oimm_new;
		} else {
			/* NVDisplay (GV100-) */
			udisp->impl.chan.wndw.oclass = disp->func->user.wndw.oclass;
			udisp->impl.chan.wndw.new = nvkm_udisp_wndw_new;
			udisp->impl.chan.wimm.oclass = disp->func->user.wimm.oclass;
			udisp->impl.chan.wimm.new = nvkm_udisp_wimm_new;
		}
	}

	*pimpl = &udisp->impl;
	*ppriv = udisp;
	*pobject = &udisp->object;
	return 0;
}
