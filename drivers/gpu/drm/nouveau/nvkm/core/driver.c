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

/*******************************************************************************
 * NVIF client driver - NVKM directly linked
 ******************************************************************************/
#include <core/client.h>
#include <core/driver.h>
#include <core/event.h>
#include <core/ioctl.h>

#include <nvif/driverif.h>
#include <nvif/event.h>

static void
nvkm_driver_unmap(struct nvif_client_priv *client, void __iomem *ptr, u32 size)
{
	iounmap(ptr);
}

static void __iomem *
nvkm_driver_map(struct nvif_client_priv *client, u64 handle, u32 size)
{
	return ioremap(handle, size);
}

static int
nvkm_driver_ioctl(void *priv, void *data, u32 size, void **hack)
{
	return nvkm_ioctl(priv, data, size, hack);
}

static int
nvkm_driver_resume(struct nvif_client_priv *client)
{
	return nvkm_object_init(&client->object);
}

static int
nvkm_driver_suspend(struct nvif_client_priv *client)
{
	return nvkm_object_fini(&client->object, true);
}

static int
nvkm_driver_event(u64 token, void *repv, u32 repc)
{
	struct nvif_object *object = (void *)(unsigned long)token;
	struct nvif_event *event = container_of(object, typeof(*event), object);

	if (event->func(event, repv, repc) == NVIF_EVENT_KEEP)
		return NVKM_EVENT_KEEP;

	return NVKM_EVENT_DROP;
}

static const struct nvif_driver
nvkm_driver = {
	.name = "nvkm",
	.suspend = nvkm_driver_suspend,
	.resume = nvkm_driver_resume,
	.ioctl = nvkm_driver_ioctl,
	.map = nvkm_driver_map,
	.unmap = nvkm_driver_unmap,
};

int
nvkm_driver_ctor(struct nvkm_device *device, const struct nvif_driver **pdrv,
		 const struct nvif_client_impl **pimpl, struct nvif_client_priv **ppriv)
{
	int ret;

	ret = nvkm_client_new("driver", device, nvkm_driver_event, pimpl, ppriv);
	if (ret)
		return ret;

	*pdrv = &nvkm_driver;
	return 0;
}
