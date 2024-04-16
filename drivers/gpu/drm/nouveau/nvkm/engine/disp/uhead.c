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

#include <nvif/if0013.h>

#include <nvif/event.h>

struct nvif_head_priv {
	struct nvkm_object object;
	struct nvkm_head *head;
};

static int
nvkm_uhead_uevent(struct nvkm_object *object, void *argv, u32 argc, struct nvkm_uevent *uevent)
{
	struct nvkm_head *head = container_of(object, struct nvif_head_priv, object)->head;
	union nvif_head_event_args *args = argv;

	if (!uevent)
		return 0;
	if (argc != sizeof(args->vn))
		return -ENOSYS;

	return nvkm_uevent_add(uevent, &head->disp->vblank, head->id,
			       NVKM_DISP_HEAD_EVENT_VBLANK, NULL);
}

static int
nvkm_uhead_mthd_scanoutpos(struct nvkm_head *head, void *argv, u32 argc)
{
	union nvif_head_scanoutpos_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	head->func->state(head, &head->arm);
	args->v0.vtotal  = head->arm.vtotal;
	args->v0.vblanks = head->arm.vblanks;
	args->v0.vblanke = head->arm.vblanke;
	args->v0.htotal  = head->arm.htotal;
	args->v0.hblanks = head->arm.hblanks;
	args->v0.hblanke = head->arm.hblanke;

	/* We don't support reading htotal/vtotal on pre-NV50 VGA,
	 * so we have to give up and trigger the timestamping
	 * fallback in the drm core.
	 */
	if (!args->v0.vtotal || !args->v0.htotal)
		return -ENOTSUPP;

	args->v0.time[0] = ktime_to_ns(ktime_get());
	head->func->rgpos(head, &args->v0.hline, &args->v0.vline);
	args->v0.time[1] = ktime_to_ns(ktime_get());
	return 0;
}

static int
nvkm_uhead_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_head *head = container_of(object, struct nvif_head_priv, object)->head;

	switch (mthd) {
	case NVIF_HEAD_V0_SCANOUTPOS: return nvkm_uhead_mthd_scanoutpos(head, argv, argc);
	default:
		return -EINVAL;
	}
}

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
	.mthd = nvkm_uhead_mthd,
	.uevent = nvkm_uhead_uevent,
};

#include "udisp.h"
int
nvkm_uhead_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = container_of(oclass->parent, struct nvif_disp_priv, object)->disp;
	struct nvkm_head *head;
	union nvif_head_args *args = argv;
	struct nvif_head_priv *uhead;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!(head = nvkm_head_find(disp, args->v0.id)))
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

	nvkm_object_ctor(&nvkm_uhead, oclass, &uhead->object);
	uhead->head = head;
	*pobject = &uhead->object;
	return 0;
}
