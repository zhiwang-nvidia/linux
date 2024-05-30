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
#include "core.h"
#include "handles.h"

#include "nouveau_bo.h"

#include <nvif/class.h>
#include <nvif/cl0002.h>

void
nv50_core_del(struct nv50_core **pcore)
{
	struct nv50_core *core = *pcore;
	if (core) {
		nvif_ctxdma_dtor(&core->vram);
		nvif_ctxdma_dtor(&core->sync);
		nvif_dispchan_dtor(&core->chan);
		kfree(*pcore);
		*pcore = NULL;
	}
}

int
nv50_core_new(struct nouveau_drm *drm, struct nv50_core **pcore)
{
	int (*ctor)(struct nouveau_drm *, s32, struct nv50_core **);
	struct nv50_disp *disp = nv50_disp(&drm->dev);
	struct nv50_core *core;
	int ret;

	switch (disp->disp->impl->chan.core.oclass) {
	case AD102_DISP_CORE_CHANNEL_DMA: ctor = corec57d_new; break;
	case GA102_DISP_CORE_CHANNEL_DMA: ctor = corec57d_new; break;
	case TU102_DISP_CORE_CHANNEL_DMA: ctor = corec57d_new; break;
	case GV100_DISP_CORE_CHANNEL_DMA: ctor = corec37d_new; break;
	case GP102_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GP100_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GM200_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GM107_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GK110_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GK104_DISP_CORE_CHANNEL_DMA: ctor = core917d_new; break;
	case GF110_DISP_CORE_CHANNEL_DMA: ctor = core907d_new; break;
	case GT214_DISP_CORE_CHANNEL_DMA: ctor = core827d_new; break;
	case GT206_DISP_CORE_CHANNEL_DMA: ctor = core827d_new; break;
	case GT200_DISP_CORE_CHANNEL_DMA: ctor = core827d_new; break;
	case   G82_DISP_CORE_CHANNEL_DMA: ctor = core827d_new; break;
	case  NV50_DISP_CORE_CHANNEL_DMA: ctor = core507d_new; break;
	default:
		NV_ERROR(drm, "No supported core channel class\n");
		return -ENODEV;
	}

	ret = ctor(drm, disp->disp->impl->chan.core.oclass, &core);
	*pcore = core;
	if (ret)
		return ret;

	ret = nvif_dispchan_ctxdma_ctor(&core->chan, "kmsCoreSyncCtxdma", NV50_DISP_HANDLE_SYNCBUF,
					NV_DMA_IN_MEMORY, &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = disp->sync->offset + 0x0000,
						.limit = disp->sync->offset + 0x0fff,
					}, sizeof(struct nv_dma_v0), &core->sync);
	if (ret)
		return ret;

	ret = nvif_dispchan_ctxdma_ctor(&core->chan, "kmsCoreVramCtxdma", NV50_DISP_HANDLE_VRAM,
					NV_DMA_IN_MEMORY, &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = 0,
						.limit = drm->device.impl->ram_user - 1,
					}, sizeof(struct nv_dma_v0), &core->vram);
	if (ret)
		return ret;

	return 0;
}
