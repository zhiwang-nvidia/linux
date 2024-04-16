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
#include <nvif/event.h>
#include <nvif/driverif.h>
#include <nvif/printf.h>

int
nvif_event_block(struct nvif_event *event)
{
	int ret;

	if (!event->impl)
		return 0;

	ret = event->impl->block(event->priv);
	NVIF_ERRON(ret, &event->object, "[BLOCK]");
	return ret;
}

int
nvif_event_allow(struct nvif_event *event)
{
	int ret;

	if (!event->impl)
		return 0;

	ret = event->impl->allow(event->priv);
	NVIF_ERRON(ret, &event->object, "[ALLOW]");
	return ret;
}

void
nvif_event_dtor(struct nvif_event *event)
{
	if (!event->impl)
		return;

	event->impl->del(event->priv);
	event->impl = NULL;
}
