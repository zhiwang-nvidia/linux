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
#include "uconn.h"
#include "outp.h"

#include <core/client.h>
#include <core/event.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>

#include <nvif/if0011.h>

struct nvif_conn_priv {
	struct nvkm_object object;
	struct nvkm_conn *conn;

	struct nvif_conn_impl impl;
};

static int
nvkm_uconn_uevent_gsp(struct nvkm_object *object, u64 token, u32 bits)
{
	union nvif_conn_event_args args;

	args.v0.version = 0;
	args.v0.types = 0;
	if (bits & NVKM_DPYID_PLUG)
		args.v0.types |= NVIF_CONN_EVENT_V0_PLUG;
	if (bits & NVKM_DPYID_UNPLUG)
		args.v0.types |= NVIF_CONN_EVENT_V0_UNPLUG;
	if (bits & NVKM_DPYID_IRQ)
		args.v0.types |= NVIF_CONN_EVENT_V0_IRQ;

	return object->client->event(token, &args, sizeof(args.v0));
}

static int
nvkm_uconn_uevent_aux(struct nvkm_object *object, u64 token, u32 bits)
{
	union nvif_conn_event_args args;

	args.v0.version = 0;
	args.v0.types = 0;
	if (bits & NVKM_I2C_PLUG)
		args.v0.types |= NVIF_CONN_EVENT_V0_PLUG;
	if (bits & NVKM_I2C_UNPLUG)
		args.v0.types |= NVIF_CONN_EVENT_V0_UNPLUG;
	if (bits & NVKM_I2C_IRQ)
		args.v0.types |= NVIF_CONN_EVENT_V0_IRQ;

	return object->client->event(token, &args, sizeof(args.v0));
}

static int
nvkm_uconn_uevent_gpio(struct nvkm_object *object, u64 token, u32 bits)
{
	union nvif_conn_event_args args;

	args.v0.version = 0;
	args.v0.types = 0;
	if (bits & NVKM_GPIO_HI)
		args.v0.types |= NVIF_CONN_EVENT_V0_PLUG;
	if (bits & NVKM_GPIO_LO)
		args.v0.types |= NVIF_CONN_EVENT_V0_UNPLUG;

	return object->client->event(token, &args, sizeof(args.v0));
}

static bool
nvkm_connector_is_dp_dms(u8 type)
{
	switch (type) {
	case DCB_CONNECTOR_DMS59_DP0:
	case DCB_CONNECTOR_DMS59_DP1:
		return true;
	default:
		return false;
	}
}

static int
nvkm_uconn_uevent(struct nvkm_object *object, void *argv, u32 argc, struct nvkm_uevent *uevent)
{
	struct nvkm_conn *conn = container_of(object, struct nvif_conn_priv, object)->conn;
	struct nvkm_disp *disp = conn->disp;
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_outp *outp;
	union nvif_conn_event_args *args = argv;
	u64 bits = 0;

	if (!uevent) {
		if (!disp->rm.client.gsp && conn->info.hpd == DCB_GPIO_UNUSED)
			return -ENOSYS;
		return 0;
	}

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	list_for_each_entry(outp, &conn->disp->outps, head) {
		if (outp->info.connector == conn->index)
			break;
	}

	if (&outp->head == &conn->disp->outps)
		return -EINVAL;

	if (disp->rm.client.gsp) {
		if (args->v0.types & NVIF_CONN_EVENT_V0_PLUG  ) bits |= NVKM_DPYID_PLUG;
		if (args->v0.types & NVIF_CONN_EVENT_V0_UNPLUG) bits |= NVKM_DPYID_UNPLUG;
		if (args->v0.types & NVIF_CONN_EVENT_V0_IRQ   ) bits |= NVKM_DPYID_IRQ;

		return nvkm_uevent_add(uevent, &disp->rm.event, outp->index, bits,
				       nvkm_uconn_uevent_gsp);
	}

	if (outp->dp.aux && !outp->info.location) {
		if (args->v0.types & NVIF_CONN_EVENT_V0_PLUG  ) bits |= NVKM_I2C_PLUG;
		if (args->v0.types & NVIF_CONN_EVENT_V0_UNPLUG) bits |= NVKM_I2C_UNPLUG;
		if (args->v0.types & NVIF_CONN_EVENT_V0_IRQ   ) bits |= NVKM_I2C_IRQ;

		return nvkm_uevent_add(uevent, &device->i2c->event, outp->dp.aux->id, bits,
				       nvkm_uconn_uevent_aux);
	}

	if (args->v0.types & NVIF_CONN_EVENT_V0_PLUG  ) bits |= NVKM_GPIO_HI;
	if (args->v0.types & NVIF_CONN_EVENT_V0_UNPLUG) bits |= NVKM_GPIO_LO;
	if (args->v0.types & NVIF_CONN_EVENT_V0_IRQ) {
		/* TODO: support DP IRQ on ANX9805 and remove this hack. */
		if (!outp->info.location && !nvkm_connector_is_dp_dms(conn->info.type))
			return -EINVAL;
	}

	return nvkm_uevent_add(uevent, &device->gpio->event, conn->info.hpd, bits,
			       nvkm_uconn_uevent_gpio);
}

static void
nvkm_uconn_del(struct nvif_conn_priv *uconn)
{
	struct nvkm_object *object = &uconn->object;

	nvkm_object_del(&object);
}

static const struct nvif_conn_impl
nvkm_uconn_impl = {
	.del = nvkm_uconn_del,
};

static void *
nvkm_uconn_dtor(struct nvkm_object *object)
{
	struct nvif_conn_priv *uconn = container_of(object, typeof(*uconn), object);
	struct nvkm_disp *disp = uconn->conn->disp;

	spin_lock(&disp->user.lock);
	uconn->conn->user = false;
	spin_unlock(&disp->user.lock);
	return uconn;
}

static const struct nvkm_object_func
nvkm_uconn = {
	.dtor = nvkm_uconn_dtor,
	.uevent = nvkm_uconn_uevent,
};

int
nvkm_uconn_new(struct nvkm_disp *disp, u8 id, const struct nvif_conn_impl **pimpl,
	       struct nvif_conn_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_conn *cont, *conn = NULL;
	struct nvif_conn_priv *uconn;
	enum nvif_conn_type type;

	list_for_each_entry(cont, &disp->conns, head) {
		if (cont->index == id) {
			conn = cont;
			break;
		}
	}

	if (!conn)
		return -EINVAL;

	switch (conn->info.type) {
	case DCB_CONNECTOR_VGA      : type = NVIF_CONN_VGA; break;
	case DCB_CONNECTOR_TV_0     :
	case DCB_CONNECTOR_TV_1     :
	case DCB_CONNECTOR_TV_3     : type = NVIF_CONN_TV; break;
	case DCB_CONNECTOR_DMS59_0  :
	case DCB_CONNECTOR_DMS59_1  :
	case DCB_CONNECTOR_DVI_I    : type = NVIF_CONN_DVI_I; break;
	case DCB_CONNECTOR_DVI_D    : type = NVIF_CONN_DVI_D; break;
	case DCB_CONNECTOR_LVDS     : type = NVIF_CONN_LVDS; break;
	case DCB_CONNECTOR_LVDS_SPWG: type = NVIF_CONN_LVDS_SPWG; break;
	case DCB_CONNECTOR_DMS59_DP0:
	case DCB_CONNECTOR_DMS59_DP1:
	case DCB_CONNECTOR_DP       :
	case DCB_CONNECTOR_mDP      :
	case DCB_CONNECTOR_USB_C    : type = NVIF_CONN_DP; break;
	case DCB_CONNECTOR_eDP      : type = NVIF_CONN_EDP; break;
	case DCB_CONNECTOR_HDMI_0   :
	case DCB_CONNECTOR_HDMI_1   :
	case DCB_CONNECTOR_HDMI_C   : type = NVIF_CONN_HDMI; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	uconn = kzalloc(sizeof(*uconn), GFP_KERNEL);
	if (!uconn)
		return -ENOMEM;

	spin_lock(&disp->user.lock);
	if (conn->user) {
		spin_unlock(&disp->user.lock);
		kfree(uconn);
		return -EBUSY;
	}
	conn->user = true;
	spin_unlock(&disp->user.lock);

	nvkm_object_ctor(&nvkm_uconn, &(struct nvkm_oclass) {}, &uconn->object);
	uconn->impl = nvkm_uconn_impl;
	uconn->impl.type = type;
	uconn->conn = conn;

	*pimpl = &uconn->impl;
	*ppriv = uconn;
	*pobject = &uconn->object;
	return 0;
}
