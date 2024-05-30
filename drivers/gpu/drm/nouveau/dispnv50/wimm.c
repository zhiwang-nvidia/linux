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
#include "wimm.h"

#include <nvif/class.h>

int
nv50_wimm_init(struct nouveau_drm *drm, struct nv50_wndw *wndw)
{
	int (*ctor)(struct nouveau_drm *, s32, struct nv50_wndw *);
	struct nvif_disp *disp = nv50_disp(&drm->dev)->disp;

	switch (disp->impl->chan.wimm.oclass) {
	case GA102_DISP_WINDOW_IMM_CHANNEL_DMA: ctor = wimmc37b_init; break;
	case TU102_DISP_WINDOW_IMM_CHANNEL_DMA: ctor = wimmc37b_init; break;
	case GV100_DISP_WINDOW_IMM_CHANNEL_DMA: ctor = wimmc37b_init; break;
	default:
		NV_ERROR(drm, "No supported window immediate class\n");
		return -ENODEV;
	}

	return ctor(drm, disp->impl->chan.wimm.oclass, wndw);
}
