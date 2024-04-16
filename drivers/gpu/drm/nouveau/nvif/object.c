/*
 * Copyright 2014 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <nvif/object.h>
#include <nvif/client.h>
#include <nvif/driverif.h>
#include <nvif/ioctl.h>

int
nvif_object_ioctl(struct nvif_object *object, void *data, u32 size, void **hack)
{
	struct nvif_client *client = object->client;
	union {
		struct nvif_ioctl_v0 v0;
	} *args = data;

	if (size >= sizeof(*args) && args->v0.version == 0) {
		if (object != &client->object)
			args->v0.object = nvif_handle(object);
		else
			args->v0.object = 0;
	} else
		return -ENOSYS;

	return client->driver->ioctl(client->object.priv, data, size, hack);
}

int
nvif_object_unmap_cpu(struct nvif_map *map)
{
	struct nvif_client *client;

	if (!map->ptr || map->impl->type == NVIF_MAP_VA)
		return 0;
	if (map->impl->type != NVIF_MAP_IO)
		return -EINVAL;

	client = map->object->client;
	client->driver->unmap(client->priv, map->ptr, map->impl->length);
	map->ptr = NULL;
	return 0;
}

int
nvif_object_map_cpu(struct nvif_object *object,
		    const struct nvif_mapinfo *impl, struct nvif_map *map)
{
	struct nvif_client *client = object->client;
	void *ptr = NULL;

	switch (impl->type) {
	case NVIF_MAP_IO:
		ptr = client->driver->map(client->priv, impl->handle, impl->length);
		break;
	case NVIF_MAP_VA:
		ptr = (void **)(unsigned long)impl->handle;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (!ptr)
		return -EFAULT;

	map->object = object;
	map->impl = impl;
	map->ptr = ptr;

	object->map.ptr = map->ptr; /*FIXME: needed by nvif_rd/wr */
	return 0;
}

void
nvif_object_dtor(struct nvif_object *object)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_del del;
	} args = {
		.ioctl.type = NVIF_IOCTL_V0_DEL,
	};

	if (!nvif_object_constructed(object))
		return;

	nvif_object_ioctl(object, &args, sizeof(args), NULL);
	object->client = NULL;
}

void
nvif_object_ctor_1(struct nvif_object *parent, const char *name, u32 handle, s32 oclass,
		   struct nvif_object *object)
{
	object->parent = parent->parent;
	object->client = parent->client;
	object->name = name ?: "nvifObject";
	object->handle = handle;
	object->oclass = oclass;
	object->priv = NULL;
	object->map.ptr = NULL;
	object->map.size = 0;
}

int
nvif_object_ctor_0(struct nvif_object *parent, const char *name, u32 handle,
		   s32 oclass, void *data, u32 size, struct nvif_object *object)
{
	struct {
		struct nvif_ioctl_v0 ioctl;
		struct nvif_ioctl_new_v0 new;
	} *args;
	int ret = 0;

	object->client = NULL;
	object->name = name ? name : "nvifObject";
	object->handle = handle;
	object->oclass = oclass;
	object->map.ptr = NULL;
	object->map.size = 0;

	if (parent) {
		if (!(args = kmalloc(sizeof(*args) + size, GFP_KERNEL))) {
			nvif_object_dtor(object);
			return -ENOMEM;
		}

		object->parent = parent->parent;

		args->ioctl.version = 0;
		args->ioctl.type = NVIF_IOCTL_V0_NEW;
		args->new.version = 0;
		args->new.object = nvif_handle(object);
		args->new.handle = handle;
		args->new.oclass = oclass;

		memcpy(args->new.data, data, size);
		ret = nvif_object_ioctl(parent, args, sizeof(*args) + size,
					&object->priv);
		memcpy(data, args->new.data, size);
		kfree(args);
		if (ret == 0)
			object->client = parent->client;
	}

	if (ret)
		nvif_object_dtor(object);
	return ret;
}
