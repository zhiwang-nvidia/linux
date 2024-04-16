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

#include <nvif/clb069.h>
#include <nvif/unpack.h>

struct nvif_faultbuf_priv {
	struct nvkm_object object;
	struct nvkm_fault_buffer *buffer;

	struct nvif_faultbuf_impl impl;
};

static int
nvkm_ufault_uevent(struct nvkm_object *object, void *argv, u32 argc, struct nvkm_uevent *uevent)
{
	struct nvkm_fault_buffer *buffer = container_of(object, struct nvif_faultbuf_priv, object)->buffer;
	union nvif_clb069_event_args *args = argv;

	if (!uevent)
		return 0;
	if (argc != sizeof(args->vn))
		return -ENOSYS;

	return nvkm_uevent_add(uevent, &buffer->fault->event, buffer->id,
			       NVKM_FAULT_BUFFER_EVENT_PENDING, NULL);
}

static int
nvkm_ufault_map(struct nvkm_object *object, void *argv, u32 argc,
		enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_fault_buffer *buffer = container_of(object, struct nvif_faultbuf_priv, object)->buffer;
	struct nvkm_device *device = buffer->fault->subdev.device;
	*type = NVKM_OBJECT_MAP_IO;
	*addr = device->func->resource_addr(device, 3) + buffer->addr;
	*size = nvkm_memory_size(buffer->mem);
	return 0;
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
	.map = nvkm_ufault_map,
	.uevent = nvkm_ufault_uevent,
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
	ufault->impl.entries = ufault->buffer->entries;
	ufault->impl.get = ufault->buffer->get;
	ufault->impl.put = ufault->buffer->put;

	*pimpl = &ufault->impl;
	*ppriv = ufault;
	*pobject = &ufault->object;
	return 0;
}
