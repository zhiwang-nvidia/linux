/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include <nvif/push006c.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/if0020.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_bo.h"
#include "nouveau_chan.h"
#include "nouveau_fence.h"
#include "nouveau_abi16.h"
#include "nouveau_vmm.h"
#include "nouveau_svm.h"

MODULE_PARM_DESC(vram_pushbuf, "Create DMA push buffers in VRAM");
int nouveau_vram_pushbuf;
module_param_named(vram_pushbuf, nouveau_vram_pushbuf, int, 0400);

void
nouveau_channel_kill(struct nouveau_channel *chan)
{
	atomic_set(&chan->killed, 1);
	if (chan->fence)
		nouveau_fence_context_kill(chan->fence, -ENODEV);
}

static int
nouveau_channel_killed(struct nvif_event *event, void *repv, u32 repc)
{
	struct nouveau_channel *chan = container_of(event, typeof(*chan), kill);
	struct nouveau_cli *cli = chan->cli;

	NV_PRINTK(warn, cli, "channel %d killed!\n", chan->chid);

	if (unlikely(!atomic_read(&chan->killed)))
		nouveau_channel_kill(chan);

	return NVIF_EVENT_DROP;
}

int
nouveau_channel_idle(struct nouveau_channel *chan)
{
	if (likely(chan && chan->fence && !atomic_read(&chan->killed))) {
		struct nouveau_cli *cli = chan->cli;
		struct nouveau_fence *fence = NULL;
		int ret;

		ret = nouveau_fence_new(&fence, chan);
		if (!ret) {
			ret = nouveau_fence_wait(fence, false, false);
			nouveau_fence_unref(&fence);
		}

		if (ret) {
			NV_PRINTK(err, cli, "failed to idle channel %d [%s]\n",
				  chan->chid, cli->name);
			return ret;
		}
	}
	return 0;
}

void
nouveau_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan = *pchan;
	if (chan) {
		if (chan->fence)
			nouveau_fence(chan->cli->drm)->context_del(chan);

		if (chan->chan.impl)
			nouveau_svmm_part(chan->vmm->svmm, chan->inst);

		nvif_object_dtor(&chan->blit);
		nvif_object_dtor(&chan->nvsw);
		nvif_object_dtor(&chan->gart);
		nvif_object_dtor(&chan->vram);
		nvif_event_dtor(&chan->kill);
		nvif_object_unmap_cpu(&chan->userd.map);
		nvif_chan_dtor(&chan->chan);
		nvif_mem_dtor(&chan->userd.mem);
		nvif_ctxdma_dtor(&chan->push.ctxdma);
		nouveau_vma_del(&chan->push.vma);
		nouveau_bo_unmap(chan->push.buffer);
		if (chan->push.buffer && chan->push.buffer->bo.pin_count)
			nouveau_bo_unpin(chan->push.buffer);
		nouveau_bo_ref(NULL, &chan->push.buffer);
		kfree(chan);
	}
	*pchan = NULL;
}

static int
nouveau_channel_kick(struct nvif_push *push)
{
	struct nouveau_channel *chan = container_of(push, typeof(*chan), chan.push);
	chan->dma.cur = chan->dma.cur + (chan->chan.push.cur - chan->chan.push.bgn);
	FIRE_RING(chan);
	chan->chan.push.bgn = chan->chan.push.cur;
	return 0;
}

static int
nouveau_channel_wait(struct nvif_push *push, u32 size)
{
	struct nouveau_channel *chan = container_of(push, typeof(*chan), chan.push);
	int ret;
	chan->dma.cur = chan->dma.cur + (chan->chan.push.cur - chan->chan.push.bgn);
	ret = RING_SPACE(chan, size);
	if (ret == 0) {
		chan->chan.push.bgn = chan->chan.push.map.ptr;
		chan->chan.push.bgn = chan->chan.push.bgn + chan->dma.cur;
		chan->chan.push.cur = chan->chan.push.bgn;
		chan->chan.push.end = chan->chan.push.bgn + size;
	}
	return ret;
}

static int
nouveau_channel_prep(struct nouveau_cli *cli,
		     u32 size, struct nouveau_channel **pchan)
{
	struct nouveau_drm *drm = cli->drm;
	struct nvif_device *device = &cli->device;
	struct nv_dma_v0 args = {};
	struct nouveau_channel *chan;
	u32 target;
	int ret;

	chan = *pchan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->cli = cli;
	chan->vmm = nouveau_cli_vmm(cli);
	atomic_set(&chan->killed, 0);

	/* allocate memory for dma push buffer */
	target = NOUVEAU_GEM_DOMAIN_GART | NOUVEAU_GEM_DOMAIN_COHERENT;
	if (nouveau_vram_pushbuf)
		target = NOUVEAU_GEM_DOMAIN_VRAM;

	ret = nouveau_bo_new(cli, size, 0, target, 0, 0, NULL, NULL,
			    &chan->push.buffer);
	if (ret == 0) {
		ret = nouveau_bo_pin(chan->push.buffer, target, false);
		if (ret == 0)
			ret = nouveau_bo_map(chan->push.buffer);
	}

	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	chan->chan.push.mem.object.parent = cli->base.object.parent;
	chan->chan.push.mem.object.client = &cli->base;
	chan->chan.push.mem.object.name = "chanPush";
	chan->chan.push.map.ptr = chan->push.buffer->kmap.virtual;
	chan->chan.push.wait = nouveau_channel_wait;
	chan->chan.push.kick = nouveau_channel_kick;

	/* create dma object covering the *entire* memory space that the
	 * pushbuf lives in, this is because the GEM code requires that
	 * we be able to call out to other (indirect) push buffers
	 */
	chan->push.addr = chan->push.buffer->offset;

	if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_vma_new(chan->push.buffer, chan->vmm,
				      &chan->push.vma);
		if (ret) {
			nouveau_channel_del(pchan);
			return ret;
		}

		chan->push.addr = chan->push.vma->addr;

		if (device->info.family >= NV_DEVICE_INFO_V0_FERMI)
			return 0;

		args.target = NV_DMA_V0_TARGET_VM;
		args.access = NV_DMA_V0_ACCESS_VM;
		args.start = 0;
		args.limit = chan->vmm->vmm.impl->limit - 1;
	} else
	if (chan->push.buffer->bo.resource->mem_type == TTM_PL_VRAM) {
		if (device->info.family == NV_DEVICE_INFO_V0_TNT) {
			/* nv04 vram pushbuf hack, retarget to its location in
			 * the framebuffer bar rather than direct vram access..
			 * nfi why this exists, it came from the -nv ddx.
			 */
			args.target = NV_DMA_V0_TARGET_PCI;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = nvxx_device(drm)->func->resource_addr(nvxx_device(drm), 1);
			args.limit = args.start + device->info.ram_user - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VRAM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = device->info.ram_user - 1;
		}
	} else {
		if (drm->agp.bridge) {
			args.target = NV_DMA_V0_TARGET_AGP;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = drm->agp.base;
			args.limit = drm->agp.base + drm->agp.size - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = chan->vmm->vmm.impl->limit - 1;
		}
	}

	ret = nvif_device_ctxdma_ctor(device, "abi16PushCtxDma", NV_DMA_FROM_MEMORY,
				      &args, sizeof(args), &chan->push.ctxdma);
	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	return 0;
}

static int
nouveau_channel_ctor(struct nouveau_cli *cli, bool priv, u64 runm,
		     struct nouveau_channel **pchan)
{
	struct nvif_device *device = &cli->device;
	const u32 oclass = device->impl->fifo.chan.oclass;
	struct nouveau_channel *chan;
	const u64 plength = 0x10000;
	const u64 ioffset = plength;
	const u64 ilength = 0x02000;
	char name[TASK_COMM_LEN];
	int ret;
	u64 size;
	struct nvif_vmm_priv *vmm = NULL;
	struct nvif_ctxdma_priv *ctxdma = NULL;
	struct nvif_mem_priv *userd = NULL;
	u64 offset, length = 0;
	u16 userd_offset = 0;

	switch (oclass) {
	case  AMPERE_CHANNEL_GPFIFO_B:
	case  AMPERE_CHANNEL_GPFIFO_A:
	case  TURING_CHANNEL_GPFIFO_A:
	case   VOLTA_CHANNEL_GPFIFO_A:
	case  PASCAL_CHANNEL_GPFIFO_A:
	case MAXWELL_CHANNEL_GPFIFO_A:
	case  KEPLER_CHANNEL_GPFIFO_B:
	case  KEPLER_CHANNEL_GPFIFO_A:
	case   FERMI_CHANNEL_GPFIFO  :
	case     G82_CHANNEL_GPFIFO  :
	case    NV50_CHANNEL_GPFIFO  :
	case    NV40_CHANNEL_DMA     :
	case    NV17_CHANNEL_DMA     :
	case    NV10_CHANNEL_DMA     :
	case    NV03_CHANNEL_DMA     :
		break;
	default:
		NV_PRINTK(err, cli, "No supported host channel class (0x%04x)", oclass);
		return -ENODEV;
	}

	if (oclass < NV50_CHANNEL_GPFIFO)
		size = plength;
	else
		size = ioffset + ilength;

	/* allocate dma push buffer */
	ret = nouveau_channel_prep(cli, size, &chan);
	*pchan = chan;
	if (ret)
		return ret;

	chan->runlist = __ffs64(runm);

	/* create channel object */
	if (oclass < NV50_CHANNEL_GPFIFO) {
		ctxdma = chan->push.ctxdma.priv;
		offset = chan->push.addr;
	} else {
		vmm = chan->vmm->vmm.priv;
		if (device->impl->fifo.chan.oclass < FERMI_CHANNEL_GPFIFO)
			ctxdma = chan->push.ctxdma.priv;

		offset = ioffset + chan->push.addr;
		length = ilength;
	}

	/* allocate userd */
	if (oclass >= VOLTA_CHANNEL_GPFIFO_A) {
		ret = nvif_mem_ctor(&cli->mmu, "abi16ChanUSERD",
				    NVIF_MEM_VRAM | NVIF_MEM_COHERENT | NVIF_MEM_MAPPABLE,
				    0, PAGE_SIZE, NULL, 0, &chan->userd.mem);
		if (ret)
			return ret;

		userd = chan->userd.mem.priv;
	}

	get_task_comm(name, current);
	snprintf(chan->name, sizeof(chan->name), "%s[%d]", name, task_pid_nr(current));

	ret = device->impl->fifo.chan.new(device->priv, device->impl->fifo.runl[chan->runlist].id,
					  0, priv, BIT(0), vmm, ctxdma, offset, length,
					  userd, userd_offset, chan->name,
					  &chan->chan.impl, &chan->chan.priv,
					  nvif_handle(&chan->chan.object));
	if (ret) {
		nouveau_channel_del(pchan);
		return ret;
	}

	nvif_chan_ctor(device, NULL, chan->name, chan->runlist, 0, &chan->chan);
	chan->chid = chan->chan.impl->id;
	chan->inst = chan->chan.impl->inst.addr;
	chan->token = chan->chan.impl->doorbell_token;
	return 0;
}

static int
nouveau_channel_init(struct nouveau_channel *chan, u32 vram, u32 gart)
{
	struct nouveau_cli *cli = chan->cli;
	struct nouveau_drm *drm = cli->drm;
	struct nvif_device *device = &cli->device;
	struct nv_dma_v0 args = {};
	int ret, i;

	if (!chan->userd.mem.impl) {
		ret = nvif_object_map_cpu(&chan->chan.object, &chan->chan.impl->map,
					  &chan->userd.map);
		if (ret)
			return ret;
	} else {
		ret = nvif_mem_map(&chan->userd.mem, NULL, 0, &chan->userd.map);
		if (ret)
			return ret;
	}

	if (chan->chan.object.oclass >= FERMI_CHANNEL_GPFIFO) {
		struct {
			struct nvif_event_v0 base;
			struct nvif_chan_event_v0 host;
		} args;

		args.host.version = 0;
		args.host.type = NVIF_CHAN_EVENT_V0_KILLED;

		ret = nvif_event_ctor(&chan->chan.object, "abi16ChanKilled", chan->chid,
				      nouveau_channel_killed, false,
				      &args.base, sizeof(args), &chan->kill);
		if (ret == 0)
			ret = nvif_event_allow(&chan->kill);
		if (ret) {
			NV_ERROR(drm, "Failed to request channel kill "
				      "notification: %d\n", ret);
			return ret;
		}
	}

	/* allocate dma objects to cover all allowed vram, and gart */
	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_VM;
			args.start = 0;
			args.limit = chan->vmm->vmm.impl->limit - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VRAM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = device->info.ram_user - 1;
		}

		ret = nvif_object_ctor(&chan->chan.object, "abi16ChanVramCtxDma", vram,
				       NV_DMA_IN_MEMORY, &args, sizeof(args),
				       &chan->vram);
		if (ret)
			return ret;

		if (device->info.family >= NV_DEVICE_INFO_V0_TESLA) {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_VM;
			args.start = 0;
			args.limit = chan->vmm->vmm.impl->limit - 1;
		} else
		if (drm->agp.bridge) {
			args.target = NV_DMA_V0_TARGET_AGP;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = drm->agp.base;
			args.limit = drm->agp.base + drm->agp.size - 1;
		} else {
			args.target = NV_DMA_V0_TARGET_VM;
			args.access = NV_DMA_V0_ACCESS_RDWR;
			args.start = 0;
			args.limit = chan->vmm->vmm.impl->limit - 1;
		}

		ret = nvif_object_ctor(&chan->chan.object, "abi16ChanGartCtxDma", gart,
				       NV_DMA_IN_MEMORY, &args, sizeof(args),
				       &chan->gart);
		if (ret)
			return ret;
	}

	/* initialise dma tracking parameters */
	switch (chan->chan.object.oclass) {
	case NV03_CHANNEL_DMA:
	case NV10_CHANNEL_DMA:
	case NV17_CHANNEL_DMA:
	case NV40_CHANNEL_DMA:
		chan->user_put = 0x40;
		chan->user_get = 0x44;
		chan->dma.max = (0x10000 / 4) - 2;
		break;
	default:
		chan->user_put = 0x40;
		chan->user_get = 0x44;
		chan->user_get_hi = 0x60;
		chan->dma.ib_base =  0x10000 / 4;
		chan->dma.ib_max  = NV50_DMA_IB_MAX;
		chan->dma.ib_put  = 0;
		chan->dma.ib_free = chan->dma.ib_max - chan->dma.ib_put;
		chan->dma.max = chan->dma.ib_base;
		break;
	}

	chan->dma.put = 0;
	chan->dma.cur = chan->dma.put;
	chan->dma.free = chan->dma.max - chan->dma.cur;

	ret = PUSH_WAIT(&chan->chan.push, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		PUSH_DATA(&chan->chan.push, 0x00000000);

	/* allocate software object class (used for fences on <= nv05) */
	if (device->info.family < NV_DEVICE_INFO_V0_CELSIUS) {
		ret = nvif_object_ctor(&chan->chan.object, "abi16NvswFence", 0x006e,
				       NVIF_CLASS_SW_NV04,
				       NULL, 0, &chan->nvsw);
		if (ret)
			return ret;

		ret = PUSH_WAIT(&chan->chan.push, 2);
		if (ret)
			return ret;

		PUSH_NVSQ(&chan->chan.push, NV_SW, 0x0000, chan->nvsw.handle);
		PUSH_KICK(&chan->chan.push);
	}

	/* initialise synchronisation */
	return nouveau_fence(drm)->context_new(chan);
}

int
nouveau_channel_new(struct nouveau_cli *cli,
		    bool priv, u64 runm, u32 vram, u32 gart, struct nouveau_channel **pchan)
{
	int ret;

	ret = nouveau_channel_ctor(cli, priv, runm, pchan);
	if (ret) {
		NV_PRINTK(dbg, cli, "channel create, %d\n", ret);
		return ret;
	}

	ret = nouveau_channel_init(*pchan, vram, gart);
	if (ret) {
		NV_PRINTK(err, cli, "channel failed to initialise, %d\n", ret);
		nouveau_channel_del(pchan);
		return ret;
	}

	ret = nouveau_svmm_join((*pchan)->vmm->svmm, (*pchan)->inst);
	if (ret)
		nouveau_channel_del(pchan);

	return ret;
}

void
nouveau_channels_fini(struct nouveau_drm *drm)
{
	kfree(drm->runl);
}

int
nouveau_channels_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->device;
	int i;

	drm->chan_nr = drm->chan_total = device->impl->fifo.chan_nr;
	drm->runl_nr = device->impl->fifo.runl_nr;
	drm->runl = kcalloc(drm->runl_nr, sizeof(*drm->runl), GFP_KERNEL);
	if (!drm->runl)
		return -ENOMEM;

	if (drm->chan_nr == 0) {
		for (i = 0; i < drm->runl_nr; i++) {
			drm->runl[i].chan_nr = device->impl->fifo.runl[i].chan_nr;
			drm->runl[i].chan_id_base = drm->chan_total;
			drm->runl[i].context_base = dma_fence_context_alloc(drm->runl[i].chan_nr);

			drm->chan_total += drm->runl[i].chan_nr;
		}
	} else {
		drm->runl[0].context_base = dma_fence_context_alloc(drm->chan_nr);
		for (i = 1; i < drm->runl_nr; i++)
			drm->runl[i].context_base = drm->runl[0].context_base;

	}

	return 0;
}
