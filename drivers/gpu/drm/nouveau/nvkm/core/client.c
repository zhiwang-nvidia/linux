/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include <core/client.h>
#include <core/device.h>
#include <core/option.h>
#include <device/user.h>

#include <nvif/class.h>
#include <nvif/driverif.h>
#include <nvif/event.h>
#include <nvif/unpack.h>

static int
nvkm_client_new_device(struct nvif_client_priv *client,
		       const struct nvif_device_impl **pimpl, struct nvif_device_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_udevice_new(client->device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link_(client, &client->object, object);
	return 0;
}

static int
nvkm_client_new_client(struct nvif_client_priv *parent,
		       const struct nvif_client_impl **pimpl, struct nvif_client_priv **ppriv)
{
	struct nvkm_client *client;
	int ret;

	ret = nvkm_client_new("client", parent->device, parent->event, pimpl, &client);
	if (ret)
		return ret;

	*ppriv = client;

	nvkm_object_link_(parent, &parent->object, &client->object);
	return 0;
}

static void
nvkm_client_del(struct nvif_client_priv *client)
{
	struct nvkm_object *object = &client->object;

	nvkm_object_del(&object);
}

const struct nvif_client_impl
nvkm_client_impl = {
	.del = nvkm_client_del,
	.client.new = nvkm_client_new_client,
	.device.new = nvkm_client_new_device,
};

static void *
nvkm_client_dtor(struct nvkm_object *object)
{
	return container_of(object, struct nvkm_client, object);
}

static const struct nvkm_object_func
nvkm_client = {
	.dtor = nvkm_client_dtor,
};

int
nvkm_client_new(const char *name, struct nvkm_device *device, int (*event)(u64, void *, u32),
		const struct nvif_client_impl **pimpl, struct nvif_client_priv **ppriv)
{
	struct nvkm_oclass oclass = {};
	struct nvkm_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	oclass.client = client;

	nvkm_object_ctor(&nvkm_client, &oclass, &client->object);
	snprintf(client->name, sizeof(client->name), "%s", name);
	client->device = device;
	client->debug = NV_DBG_ERROR;
	client->objroot = RB_ROOT;
	spin_lock_init(&client->obj_lock);
	client->event = event;

	*pimpl = &nvkm_client_impl;
	*ppriv = client;
	return 0;
}
