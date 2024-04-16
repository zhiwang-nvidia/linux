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
#include "base.h"

#include <nvif/class.h>

int
nv50_base_new(struct nouveau_drm *drm, int head, struct nv50_wndw **pwndw)
{
	int (*ctor)(struct nouveau_drm *, int, s32, struct nv50_wndw **);
	struct nvif_disp *disp = nv50_disp(drm->dev)->disp;

	switch (disp->impl->chan.base.oclass) {
	case GK110_DISP_BASE_CHANNEL_DMA: ctor = base917c_new; break;
	case GK104_DISP_BASE_CHANNEL_DMA: ctor = base917c_new; break;
	case GF110_DISP_BASE_CHANNEL_DMA: ctor = base907c_new; break;
	case GT214_DISP_BASE_CHANNEL_DMA: ctor = base827c_new; break;
	case GT200_DISP_BASE_CHANNEL_DMA: ctor = base827c_new; break;
	case   G82_DISP_BASE_CHANNEL_DMA: ctor = base827c_new; break;
	case  NV50_DISP_BASE_CHANNEL_DMA: ctor = base507c_new; break;
	default:
		NV_ERROR(drm, "No supported base class\n");
		return -ENODEV;
	}

	return ctor(drm, head, disp->impl->chan.base.oclass, pwndw);
}
