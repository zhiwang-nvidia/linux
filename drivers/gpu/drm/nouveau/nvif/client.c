/*
 * Copyright 2013 Red Hat Inc.
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

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/driverif.h>
#include <nvif/ioctl.h>
#include <nvif/printf.h>

int
nvif_client_suspend(struct nvif_client *client)
{
	return client->driver->suspend(client->priv);
}

int
nvif_client_resume(struct nvif_client *client)
{
	return client->driver->resume(client->priv);
}

void
nvif_client_dtor(struct nvif_client *client)
{
	client->impl->del(client->priv);
	client->impl = NULL;
	client->object.client = NULL;
	client->driver = NULL;
}

int
nvif_client_ctor(struct nvif_client *parent, const char *name, struct nvif_client *client)
{
	int ret;

	ret = parent->impl->client.new(parent->priv, &client->impl, &client->priv);
	NVIF_ERRON(ret, &parent->object, "[NEW client]");
	if (ret)
		return ret;

	nvif_object_ctor(&parent->object, name ?: "nvifClient", 0, 0, &client->object);
	client->object.priv = client->priv;
	client->driver = parent->driver;
	return 0;
}
