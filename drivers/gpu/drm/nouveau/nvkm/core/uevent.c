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
#include <core/event.h>
#include <core/client.h>

#include <nvif/driverif.h>

struct nvif_event_priv {
	struct nvkm_object object;
	struct nvkm_object *parent;
	nvkm_uevent_func func;
	bool wait;

	struct nvkm_event_ntfy ntfy;
	atomic_t allowed;
};

static int
nvkm_uevent_block(struct nvif_event_priv *uevent)
{
	nvkm_event_ntfy_block(&uevent->ntfy);
	atomic_set(&uevent->allowed, 0);
	return 0;
}

static int
nvkm_uevent_allow(struct nvif_event_priv *uevent)
{
	nvkm_event_ntfy_allow(&uevent->ntfy);
	atomic_set(&uevent->allowed, 1);
	return 0;
}

static void
nvkm_uevent_del(struct nvif_event_priv *uevent)
{
	struct nvkm_object *object = &uevent->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_event_impl
nvkm_uevent_impl = {
	.del = nvkm_uevent_del,
	.allow = nvkm_uevent_allow,
	.block = nvkm_uevent_block,
};

static int
nvkm_uevent_fini(struct nvkm_object *object, bool suspend)
{
	struct nvif_event_priv *uevent = container_of(object, typeof(*uevent), object);

	nvkm_event_ntfy_block(&uevent->ntfy);
	return 0;
}

static int
nvkm_uevent_init(struct nvkm_object *object)
{
	struct nvif_event_priv *uevent = container_of(object, typeof(*uevent), object);

	if (atomic_read(&uevent->allowed))
		nvkm_event_ntfy_allow(&uevent->ntfy);

	return 0;
}

static void *
nvkm_uevent_dtor(struct nvkm_object *object)
{
	struct nvif_event_priv *uevent = container_of(object, typeof(*uevent), object);

	nvkm_event_ntfy_del(&uevent->ntfy);
	return uevent;
}

static const struct nvkm_object_func
nvkm_uevent = {
	.dtor = nvkm_uevent_dtor,
	.init = nvkm_uevent_init,
	.fini = nvkm_uevent_fini,
};

static int
nvkm_uevent_ntfy(struct nvkm_event_ntfy *ntfy, u32 bits)
{
	struct nvif_event_priv *uevent = container_of(ntfy, typeof(*uevent), ntfy);
	struct nvkm_client *client = uevent->object.client;

	if (uevent->func)
		return uevent->func(uevent->parent, uevent->object.object, bits);

	return client->event(uevent->object.object, NULL, 0);
}

int
nvkm_uevent_add(struct nvif_event_priv *uevent, struct nvkm_event *event, int id, u32 bits,
		nvkm_uevent_func func)
{
	if (WARN_ON(uevent->func))
		return -EBUSY;

	nvkm_event_ntfy_add(event, id, bits, uevent->wait, nvkm_uevent_ntfy, &uevent->ntfy);
	uevent->func = func;
	return 0;
}

int
nvkm_uevent_new_(struct nvkm_object *parent, u64 handle, struct nvkm_event *event,
		 bool wait, int id, u32 bits, nvkm_uevent_func func,
		 const struct nvif_event_impl **pimpl, struct nvif_event_priv **ppriv)
{
	struct nvif_event_priv *uevent;
	int ret;

	uevent = kzalloc(sizeof(*uevent), GFP_KERNEL);
	if (!uevent)
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uevent, &(struct nvkm_oclass) {}, &uevent->object);
	uevent->object.object = handle;
	uevent->parent = parent;
	uevent->func = NULL;
	uevent->wait = wait;
	uevent->ntfy.event = NULL;

	ret = nvkm_uevent_add(uevent, event, id, bits, func);
	if (ret) {
		kfree(uevent);
		return ret;
	}

	*pimpl = &nvkm_uevent_impl;
	*ppriv = uevent;

	nvkm_object_link(parent, &uevent->object);
	return 0;
}
