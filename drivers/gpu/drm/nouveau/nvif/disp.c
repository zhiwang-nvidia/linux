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
#include <nvif/disp.h>
#include <nvif/device.h>
#include <nvif/printf.h>

#include <nvif/class.h>
#include <nvif/if0010.h>

void
nvif_disp_dtor(struct nvif_disp *disp)
{
	nvif_object_dtor(&disp->object);
}

int
nvif_disp_ctor(struct nvif_device *device, const char *name, struct nvif_disp *disp)
{
	const u32 oclass = device->impl->disp.oclass;
	struct nvif_disp_v0 args;
	int ret;

	disp->object.client = NULL;

	switch (oclass) {
	case AD102_DISP:
	case GA102_DISP:
	case TU102_DISP:
	case GV100_DISP:
	case GP102_DISP:
	case GP100_DISP:
	case GM200_DISP:
	case GM107_DISP:
	case GK110_DISP:
	case GK104_DISP:
	case GF110_DISP:
	case GT214_DISP:
	case GT206_DISP:
	case GT200_DISP:
	case   G82_DISP:
	case  NV50_DISP:
	case  NV04_DISP:
		break;
	default:
		NVIF_DEBUG(&device->object, "[NEW disp%04x] not supported", oclass);
		return -ENODEV;
	}

	args.version = 0;

	ret = nvif_object_ctor(&device->object, name ?: "nvifDisp", 0,
			       oclass, &args, sizeof(args), &disp->object);
	NVIF_ERRON(ret, &device->object, "[NEW disp%04x]", oclass);
	if (ret)
		return ret;

	NVIF_DEBUG(&disp->object, "[NEW] conn_mask:%08x outp_mask:%08x head_mask:%08x",
		   args.conn_mask, args.outp_mask, args.head_mask);
	disp->conn_mask = args.conn_mask;
	disp->outp_mask = args.outp_mask;
	disp->head_mask = args.head_mask;
	return 0;
}
