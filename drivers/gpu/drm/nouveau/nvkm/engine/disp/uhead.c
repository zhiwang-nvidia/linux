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
#include "uhead.h"
#include <core/event.h>

#include <nvif/event.h>

struct nvif_head_priv {
	struct nvkm_object object;
	struct nvkm_head *head;
};

static int
nvkm_uhead_vblank(struct nvif_head_priv *uhead, u64 handle,
		  const struct nvif_event_impl **pimpl, struct nvif_event_priv **ppriv)
{
	struct nvkm_head *head = uhead->head;

	return nvkm_uevent_new_(&uhead->object, handle, &head->disp->vblank, false, head->id,
				NVKM_DISP_HEAD_EVENT_VBLANK, NULL, pimpl, ppriv);
}

static int
nvkm_uhead_scanoutpos(struct nvif_head_priv *uhead, s64 time[2],
		      u16 *vblanks, u16 *vblanke, u16 *vtotal, u16 *vline,
		      u16 *hblanks, u16 *hblanke, u16 *htotal, u16 *hline)
{
	struct nvkm_head *head = uhead->head;

	head->func->state(head, &head->arm);
	*vtotal  = head->arm.vtotal;
	*vblanks = head->arm.vblanks;
	*vblanke = head->arm.vblanke;
	*htotal  = head->arm.htotal;
	*hblanks = head->arm.hblanks;
	*hblanke = head->arm.hblanke;

	/* We don't support reading htotal/vtotal on pre-NV50 VGA,
	 * so we have to give up and trigger the timestamping
	 * fallback in the drm core.
	 */
	if (!*vtotal || !*htotal)
		return -ENOTSUPP;

	time[0] = ktime_to_ns(ktime_get());
	head->func->rgpos(head, hline, vline);
	time[1] = ktime_to_ns(ktime_get());
	return 0;
}

static void
nvkm_uhead_del(struct nvif_head_priv *uhead)
{
	struct nvkm_object *object = &uhead->object;

	nvkm_object_del(&object);
}

static const struct nvif_head_impl
nvkm_uhead_impl = {
	.del = nvkm_uhead_del,
	.scanoutpos = nvkm_uhead_scanoutpos,
	.vblank = nvkm_uhead_vblank,
};

static void *
nvkm_uhead_dtor(struct nvkm_object *object)
{
	struct nvif_head_priv *uhead = container_of(object, struct nvif_head_priv, object);
	struct nvkm_disp *disp = uhead->head->disp;

	spin_lock(&disp->user.lock);
	uhead->head->user = false;
	spin_unlock(&disp->user.lock);
	return uhead;
}

static const struct nvkm_object_func
nvkm_uhead = {
	.dtor = nvkm_uhead_dtor,
};

int
nvkm_uhead_new(struct nvkm_disp *disp, u8 id, const struct nvif_head_impl **pimpl,
	       struct nvif_head_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_head *head;
	struct nvif_head_priv *uhead;

	if (!(head = nvkm_head_find(disp, id)))
		return -EINVAL;

	uhead = kzalloc(sizeof(*uhead), GFP_KERNEL);
	if (!uhead)
		return -ENOMEM;

	spin_lock(&disp->user.lock);
	if (head->user) {
		spin_unlock(&disp->user.lock);
		kfree(uhead);
		return -EBUSY;
	}
	head->user = true;
	spin_unlock(&disp->user.lock);

	nvkm_object_ctor(&nvkm_uhead, &(struct nvkm_oclass) {}, &uhead->object);
	uhead->head = head;

	*pimpl = &nvkm_uhead_impl;
	*ppriv = uhead;
	*pobject = &uhead->object;
	return 0;
}
