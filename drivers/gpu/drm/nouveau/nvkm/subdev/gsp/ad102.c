/*
 * Copyright 2023 Red Hat Inc.
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

#include <core/pci.h>
#include <engine/sec2.h>
#include "priv.h"

static bool is_scrubber_completed(struct nvkm_gsp *gsp)
{
	return ((nvkm_rd32(gsp->subdev.device, 0x001180fc) >> 29) >= 0x3);
}

static int
ad102_execute_scrubber(struct nvkm_gsp *gsp)
{
	struct nvkm_falcon_fw fw = {0};
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	int ret;

	if (!gsp->fws.scrubber || is_scrubber_completed(gsp))
		return 0;

	ret = gsp->func->booter.ctor(gsp, "scrubber", gsp->fws.scrubber,
				     &device->sec2->falcon, &fw);
	if (ret)
		return ret;

	ret = nvkm_falcon_fw_boot(&fw, subdev, true, NULL, NULL, 0, 0);
	nvkm_falcon_fw_dtor(&fw);
	if (ret)
		return ret;

	if (WARN_ON(!is_scrubber_completed(gsp)))
		return -ENOSPC;

	return 0;
}

static int
ad102_gsp_init_fw_heap(struct nvkm_gsp *gsp)
{
	struct nvkm_subdev *subdev = &gsp->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_device_pci *device_pci = container_of(device,
			typeof(*device_pci), device);
	int num_vfs;
	int ret;

	num_vfs = pci_sriov_get_totalvfs(device_pci->pdev);
	if (!num_vfs)
		nvkm_gsp_init_fw_heap(gsp, 0);
	else
		nvkm_gsp_init_fw_heap(gsp, 576 * SZ_1M);

	if (gsp->fb.wpr2.heap.size <= SZ_256M)
		return 0;

	/* Load scrubber ucode image */
	ret = r535_gsp_load_fw(gsp, "scrubber", gsp->fwif->ver,
			       &gsp->fws.scrubber);
	if (ret)
		return ret;

	return 0;
}

static const struct nvkm_gsp_func
ad102_gsp_r535_113_01 = {
	.flcn = &ga102_gsp_flcn,
	.fwsec = &ga102_gsp_fwsec,

	.sig_section = ".fwsignature_ad10x",

	.wpr_heap.os_carveout_size = 20 << 20,
	.wpr_heap.base_size = 8 << 20,
	.wpr_heap.min_size = 84 << 20,
	.wpr_heap.init_fw_heap = ad102_gsp_init_fw_heap,
	.wpr_heap.execute_scrubber = ad102_execute_scrubber,

	.booter.ctor = ga102_gsp_booter_ctor,

	.dtor = r535_gsp_dtor,
	.oneinit = tu102_gsp_oneinit,
	.init = r535_gsp_init,
	.fini = r535_gsp_fini,
	.reset = ga102_gsp_reset,

	.rm = &r535_gsp_rm,
};

static struct nvkm_gsp_fwif
ad102_gsps[] = {
	{ 0, r535_gsp_load, &ad102_gsp_r535_113_01, "535.113.01", true },
	{}
};

int
ad102_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(ad102_gsps, device, type, inst, pgsp);
}
