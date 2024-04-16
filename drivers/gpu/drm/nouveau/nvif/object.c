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
	if (!nvif_object_constructed(object))
		return;

	object->client = NULL;
}

void
nvif_object_ctor(struct nvif_object *parent, const char *name, u32 handle, s32 oclass,
		 struct nvif_object *object)
{
	object->parent = parent->parent;
	object->client = parent->client;
	object->name = name ?: "nvifObject";
	object->handle = handle;
	object->oclass = oclass;
	object->map.ptr = NULL;
	object->map.size = 0;
}
