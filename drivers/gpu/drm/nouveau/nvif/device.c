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
#include <nvif/device.h>
#include <nvif/client.h>
#include <nvif/printf.h>

u64
nvif_device_time(struct nvif_device *device)
{
	if (!device->user.func)
		return device->impl->time(device->priv);

	return device->user.func->time(&device->user);
}

int
nvif_device_map(struct nvif_device *device)
{
	return nvif_object_map_cpu(&device->object, &device->impl->map, &device->map);
}

void
nvif_device_dtor(struct nvif_device *device)
{
	if (!device->impl)
		return;

	nvif_user_dtor(device);

	nvif_object_unmap_cpu(&device->map);

	device->impl->del(device->priv);
	device->impl = NULL;
}

int
nvif_device_ctor(struct nvif_client *client, const char *name, struct nvif_device *device)
{
	int ret;

	device->user.func = NULL;

	ret = client->impl->device.new(client->priv, &device->impl, &device->priv,
				       nvif_handle(&device->object));
	NVIF_ERRON(ret, &client->object, "[NEW device]");
	if (ret)
		return ret;

	nvif_object_ctor(&client->object, name ?: "nvifDevice", 0, 0, &device->object);
	device->object.client = client;

	if (ret == 0) {
		/*FIXME: remove after moving users to device->impl */
		device->info.version = 0;
		device->info.platform = device->impl->platform;
		device->info.chipset = device->impl->chipset;
		device->info.revision = device->impl->revision;
		device->info.family = device->impl->family;
		device->info.ram_size = device->impl->ram_size;
		device->info.ram_user = device->impl->ram_user;
		strscpy(device->info.chip, device->impl->chip, sizeof(device->info.chip));
		strscpy(device->info.name, device->impl->name, sizeof(device->info.name));
	}

	return ret;
}
