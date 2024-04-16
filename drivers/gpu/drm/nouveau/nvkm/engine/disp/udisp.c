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
#include "chan.h"
#include "conn.h"
#include "head.h"
#include "outp.h"

#include <nvif/class.h>
#include <nvif/if0010.h>

static int
nvkm_udisp_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *sclass)
{
	struct nvkm_disp *disp = container_of(object, struct nvif_disp_priv, object)->disp;

	if (index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, NVIF_CLASS_CONN };
		sclass->ctor = nvkm_uconn_new;
		return 0;
	}

	if (index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, NVIF_CLASS_OUTP };
		sclass->ctor = nvkm_uoutp_new;
		return 0;
	}

	if (index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, NVIF_CLASS_HEAD };
		sclass->ctor = nvkm_uhead_new;
		return 0;
	}

	if (disp->func->user.caps.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { -1, -1, disp->func->user.caps.oclass };
		sclass->ctor = disp->func->user.caps.ctor;
		return 0;
	}

	if (disp->func->user.core.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.core.oclass };
		sclass->ctor = nvkm_disp_core_new;
		return 0;
	}

	if (disp->func->user.base.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.base.oclass };
		sclass->ctor = nvkm_disp_chan_new;
		return 0;
	}

	if (disp->func->user.ovly.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.ovly.oclass };
		sclass->ctor = nvkm_disp_chan_new;
		return 0;
	}

	if (disp->func->user.curs.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.curs.oclass };
		sclass->ctor = nvkm_disp_chan_new;
		return 0;
	}

	if (disp->func->user.oimm.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.oimm.oclass };
		sclass->ctor = nvkm_disp_chan_new;
		return 0;
	}

	if (disp->func->user.wndw.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.wndw.oclass };
		sclass->ctor = nvkm_disp_wndw_new;
		return 0;
	}

	if (disp->func->user.wimm.oclass && index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, disp->func->user.wimm.oclass };
		sclass->ctor = nvkm_disp_wndw_new;
		return 0;
	}

	return -EINVAL;
}

static void *
nvkm_udisp_dtor(struct nvkm_object *object)
{
	struct nvif_disp_priv *udisp = container_of(object, typeof(*udisp), object);
	struct nvkm_disp *disp = udisp->disp;

	spin_lock(&disp->user.lock);
	disp->user.allocated = false;
	spin_unlock(&disp->user.lock);
	return udisp;
}

static const struct nvkm_object_func
nvkm_udisp = {
	.dtor = nvkm_udisp_dtor,
	.sclass = nvkm_udisp_sclass,
};

int
nvkm_udisp_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_disp(oclass->engine);
	struct nvkm_conn *conn;
	struct nvkm_outp *outp;
	struct nvkm_head *head;
	union nvif_disp_args *args = argv;
	struct nvif_disp_priv *udisp;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	udisp = kzalloc(sizeof(*udisp), GFP_KERNEL);
	if (!udisp)
		return -ENOMEM;

	spin_lock(&disp->user.lock);
	if (disp->user.allocated) {
		spin_unlock(&disp->user.lock);
		kfree(udisp);
		return -EBUSY;
	}
	disp->user.allocated = true;
	spin_unlock(&disp->user.lock);

	nvkm_object_ctor(&nvkm_udisp, oclass, &udisp->object);
	udisp->disp = disp;
	*pobject = &udisp->object;

	args->v0.conn_mask = 0;
	list_for_each_entry(conn, &disp->conns, head)
		args->v0.conn_mask |= BIT(conn->index);

	args->v0.outp_mask = 0;
	list_for_each_entry(outp, &disp->outps, head)
		args->v0.outp_mask |= BIT(outp->index);

	args->v0.head_mask = 0;
	list_for_each_entry(head, &disp->heads, head)
		args->v0.head_mask |= BIT(head->id);

	return 0;
}
