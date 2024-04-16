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
#include <nvif/conn.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

int
nvif_conn_event_ctor(struct nvif_conn *conn, const char *name, nvif_event_func func, u8 types,
		     struct nvif_event *event)
{
	int ret;

	if (!conn->impl->event)
		return -ENODEV;

	ret = conn->impl->event(conn->priv, nvif_handle(&event->object), types,
				&event->impl, &event->priv);
	NVIF_ERRON(ret, &conn->object, "[NEW EVENT:HPD types:%02x]", types);
	if (ret)
		return ret;

	nvif_event_ctor(&conn->object, name ?: "nvifConnHpd", conn->id, func, event);
	return 0;
}

void
nvif_conn_dtor(struct nvif_conn *conn)
{
	if (!conn->impl)
		return;

	conn->impl->del(conn->priv);
	conn->impl = NULL;
}

int
nvif_conn_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_conn *conn)
{
	int ret;

	ret = disp->impl->conn.new(disp->priv, id, &conn->impl, &conn->priv);
	NVIF_ERRON(ret, &disp->object, "[NEW conn id:%d]", id);
	if (ret)
		return ret;

	nvif_object_ctor(&disp->object, name ?: "nvifConn", id, 0, &conn->object);
	conn->id = id;
	return 0;
}
