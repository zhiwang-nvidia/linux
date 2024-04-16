/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <nvif/dispchan.h>
#include <nvif/ctxdma.h>
#include <nvif/device.h>
#include <nvif/driverif.h>
#include <nvif/push507c.h>
#include <nvif/timer.h>

#include <nvif/class.h>
#include <nvhw/class/cl507c.h>

int
nvif_dispchan_ctxdma_ctor(struct nvif_dispchan *chan, const char *name, u32 handle, s32 oclass,
			  void *argv, u32 argc, struct nvif_ctxdma *ctxdma)
{
	int ret;

	ret = chan->impl->ctxdma.new(chan->priv, handle, oclass, argv, argc,
				     &ctxdma->impl, &ctxdma->priv);
	NVIF_ERRON(ret, &chan->object, "[NEW ctxdma%04x handle:%08x]", oclass, handle);
	if (ret)
		return ret;

	nvif_ctxdma_ctor(&chan->object, name ?: "nvifDispChanCtxdma", handle, oclass, ctxdma);
	return 0;
}

static int
nvif_dispchan_kick(struct nvif_push *push)
{
	struct nvif_dispchan *chan = container_of(push, typeof(*chan), push);

	push->hw.cur = push->cur - (u32 __iomem *)chan->push.map.ptr;
	if (push->hw.put != push->hw.cur) {
		/* Push buffer fetches are not coherent with BAR1, we need to ensure
		 * writes have been flushed right through to VRAM before writing PUT.
		 */
		if (chan->push.mem.type & NVIF_MEM_VRAM) {
			struct nvif_device *device = chan->disp->device;

			nvif_wr32(&device->object, 0x070000, 0x00000001);
			nvif_msec(device, 2000,
				if (!(nvif_rd32(&device->object, 0x070000) & 0x00000002))
					break;
			);
		}

		NVIF_WV32(chan, NV507C, PUT, PTR, push->hw.cur);
		push->hw.put = push->hw.cur;
	}

	push->bgn = push->cur;
	return 0;
}

static int
nvif_dispchan_free(struct nvif_dispchan *chan)
{
	struct nvif_push *push = &chan->push;
	u32 get;

	get = NVIF_RV32(chan, NV507C, GET, PTR);
	if (get > push->hw.cur) /* NVIDIA stay 5 away from GET, do the same. */
		return get - push->hw.cur - 5;

	return push->hw.max - push->hw.cur;
}

static int
nvif_dispchan_wind(struct nvif_dispchan *chan)
{
	struct nvif_push *push = &chan->push;

	/* Wait for GET to depart from the beginning of the push buffer to
	 * prevent writing PUT == GET, which would be ignored by HW.
	 */
	u32 get = NVIF_RV32(chan, NV507C, GET, PTR);
	if (get == 0) {
		/* Corner-case, HW idle, but non-committed work pending. */
		if (push->hw.put == 0)
			nvif_dispchan_kick(&chan->push);

		if (nvif_msec(chan->disp->device, 2000,
			if (NVIF_TV32(chan, NV507C, GET, PTR, >, 0))
				break;
		) < 0)
			return -ETIMEDOUT;
	}

	PUSH_RSVD(&chan->push, PUSH_JUMP(&chan->push, 0));
	push->hw.cur = 0;
	return 0;
}

static int
nvif_dispchan_wait(struct nvif_push *push, u32 size)
{
	struct nvif_dispchan *chan = container_of(push, typeof(*chan), push);
	int free;

	if (WARN_ON(size > push->hw.max))
		return -EINVAL;

	push->hw.cur = push->cur - (u32 __iomem *)chan->push.map.ptr;
	if (push->hw.cur + size >= push->hw.max) {
		int ret = nvif_dispchan_wind(chan);
		if (ret)
			return ret;

		push->cur = chan->push.map.ptr;
		push->cur = push->cur + push->hw.cur;
		nvif_dispchan_kick(push);
	}

	if (nvif_msec(chan->disp->device, 2000,
		if ((free = nvif_dispchan_free(chan)) >= size)
			break;
	) < 0) {
		WARN_ON(1);
		return -ETIMEDOUT;
	}

	push->bgn = chan->push.map.ptr;
	push->bgn = push->bgn + push->hw.cur;
	push->cur = push->bgn;
	push->end = push->cur + free;
	return 0;
}

void
nvif_dispchan_dtor(struct nvif_dispchan *chan)
{
	if (chan->impl) {
		chan->impl->del(chan->priv);
		chan->impl = NULL;
	}

	nvif_mem_unmap_dtor(&chan->push.mem, &chan->push.map);
}

int
nvif_dispchan_oneinit(struct nvif_dispchan *chan)
{
	int ret;

	ret = nvif_object_map_cpu(&chan->object, &chan->impl->map, &chan->map);
	if (ret)
		return ret;

	return 0;
}

int
nvif_dispchan_ctor(struct nvif_disp *disp, const char *name, u32 handle, s32 oclass,
		   struct nvif_mmu *mmu, struct nvif_dispchan *chan)
{
	u8 type = NVIF_MEM_COHERENT;
	int ret;

	/* PIO channels don't need a push buffer. */
	chan->push.mem.impl = NULL;
	chan->impl = NULL;
	if (!mmu)
		goto done;

	/* Pascal added support for 47-bit physical addresses, but some
	 * parts of EVO still only accept 40-bit PAs.
	 *
	 * To avoid issues on systems with large amounts of RAM, and on
	 * systems where an IOMMU maps pages at a high address, we need
	 * to allocate push buffers in VRAM instead.
	 *
	 * This appears to match NVIDIA's behaviour on Pascal.
	 */
	if (disp->device->impl->family == NVIF_DEVICE_PASCAL)
		type |= NVIF_MEM_VRAM;

	ret = nvif_mem_ctor_map(mmu, "nvifDispChanPush", type, 0x1000,
				&chan->push.mem, &chan->push.map);
	if (ret)
		return ret;

	chan->push.hw.cur = 0;
	chan->push.hw.put = 0;
	chan->push.hw.max = 0x1000/4 - 1;
	chan->push.bgn = chan->push.map.ptr;
	chan->push.cur = chan->push.bgn;
	chan->push.end = chan->push.bgn;
	chan->push.wait = nvif_dispchan_wait;
	chan->push.kick = nvif_dispchan_kick;

	/* EVO channels are affected by a HW bug where the last 12 DWORDs
	 * of the push buffer aren't able to be used safely.
	 */
	if (disp->object.oclass < GV100_DISP)
		chan->push.hw.max -= 12;

done:
	nvif_object_ctor(&disp->object, name ?: "nvifDispChan", handle, oclass, &chan->object);
	chan->disp = disp;
	return 0;
}
