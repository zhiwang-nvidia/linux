/*
 * Copyright 2018 Red Hat Inc.
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
#include "user.h"
#include "priv.h"

#include <core/memory.h>
#include <core/event.h>
#include <subdev/mmu.h>

struct nvif_faultbuf_priv {
	struct nvkm_object object;
	struct nvkm_fault_buffer *buffer;

	struct nvif_faultbuf_impl impl;
};

static int
nvkm_ufault_event_new(struct nvif_faultbuf_priv *ufault, u64 token,
		      const struct nvif_event_impl **pimpl, struct nvif_event_priv **ppriv)
{
	struct nvkm_fault_buffer *buffer = ufault->buffer;

	return nvkm_uevent_new_(&ufault->object, token, &buffer->fault->event, true, buffer->id,
				NVKM_FAULT_BUFFER_EVENT_PENDING, NULL, pimpl, ppriv);
}

static void
nvkm_ufault_del(struct nvif_faultbuf_priv *ufault)
{
	struct nvkm_object *object = &ufault->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_faultbuf_impl
nvkm_ufault_impl = {
	.del = nvkm_ufault_del,
	.event.new = nvkm_ufault_event_new,
};

static int
nvkm_ufault_fini(struct nvkm_object *object, bool suspend)
{
	struct nvif_faultbuf_priv *ufault = container_of(object, typeof(*ufault), object);
	struct nvkm_fault_buffer *buffer = ufault->buffer;

	buffer->fault->func->buffer.fini(buffer);
	return 0;
}

static int
nvkm_ufault_init(struct nvkm_object *object)
{
	struct nvif_faultbuf_priv *ufault = container_of(object, typeof(*ufault), object);
	struct nvkm_fault_buffer *buffer = ufault->buffer;

	buffer->fault->func->buffer.init(buffer);
	return 0;
}

static void *
nvkm_ufault_dtor(struct nvkm_object *object)
{
	return container_of(object, struct nvif_faultbuf_priv, object);
}

static const struct nvkm_object_func
nvkm_ufault = {
	.dtor = nvkm_ufault_dtor,
	.init = nvkm_ufault_init,
	.fini = nvkm_ufault_fini,
};

int
nvkm_ufault_new(struct nvkm_device *device, const struct nvif_faultbuf_impl **pimpl,
		struct nvif_faultbuf_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_fault *fault = device->fault;
	struct nvif_faultbuf_priv *ufault;
	int ret;

	ufault = kzalloc(sizeof(*ufault), GFP_KERNEL);
	if (!ufault)
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_ufault, &(struct nvkm_oclass) {}, &ufault->object);
	ufault->buffer = fault->buffer[fault->func->user.rp];

	ret = nvkm_ufault_init(&ufault->object);
	if (ret) {
		nvkm_ufault_del(ufault);
		return ret;
	}

	ufault->impl = nvkm_ufault_impl;
	ufault->impl.map.type = NVIF_MAP_IO;
	ufault->impl.map.handle = device->func->resource_addr(device, 3) + ufault->buffer->addr;
	ufault->impl.map.length = nvkm_memory_size(ufault->buffer->mem);
	ufault->impl.entries = ufault->buffer->entries;
	ufault->impl.get = ufault->buffer->get;
	ufault->impl.put = ufault->buffer->put;

	*pimpl = &ufault->impl;
	*ppriv = ufault;
	*pobject = &ufault->object;
	return 0;
}
