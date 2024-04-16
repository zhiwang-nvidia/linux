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
#include <nvif/user.h>
#include <nvif/device.h>
#include <nvif/driverif.h>
#include <nvif/printf.h>

#include <nvif/class.h>

void
nvif_user_dtor(struct nvif_device *device)
{
	if (device->user.impl) {
		nvif_object_unmap_cpu(&device->user.map);

		device->user.impl->del(device->user.priv);
		device->user.impl = NULL;
		device->user.func = NULL;
	}
}

int
nvif_user_ctor(struct nvif_device *device, const char *name)
{
	const u32 oclass = device->impl->usermode.oclass;
	const struct nvif_user_func *func;
	int ret;

	if (device->user.func)
		return 0;

	switch (oclass) {
	case AMPERE_USERMODE_A: func = &nvif_userc361; break;
	case TURING_USERMODE_A: func = &nvif_userc361; break;
	case  VOLTA_USERMODE_A: func = &nvif_userc361; break;
	default:
		NVIF_DEBUG(&device->object, "[NEW usermode%04x] not supported", oclass);
		return -ENODEV;
	}

	ret = device->impl->usermode.new(device->priv, &device->user.impl, &device->user.priv);
	NVIF_ERRON(ret, &device->object, "[NEW usermode%04x]", oclass);
	if (ret)
		return ret;

	nvif_object_ctor(&device->object, name ?: "nvifUsermode", 0, oclass, &device->user.object);
	device->user.func = func;

	ret = nvif_object_map_cpu(&device->user.object, &device->user.impl->map, &device->user.map);
	if (ret) {
		nvif_user_dtor(device);
		return ret;
	}

	return 0;
}
