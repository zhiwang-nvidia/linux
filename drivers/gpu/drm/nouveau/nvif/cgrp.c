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
#include <nvif/cgrp.h>
#include <nvif/device.h>
#include <nvif/vmm.h>

void
nvif_cgrp_dtor(struct nvif_cgrp *cgrp)
{
	if (!cgrp->impl)
		return;

	cgrp->impl->del(cgrp->priv);
	cgrp->impl = NULL;
}

int
nvif_cgrp_ctor(struct nvif_device *device, struct nvif_vmm *vmm, int runl, struct nvif_cgrp *cgrp)
{
	int ret;

	if (!device->impl->fifo.cgrp.new)
		return -ENODEV;

	ret = device->impl->fifo.cgrp.new(device->priv, runl, vmm->priv, "nvifCgrp",
					  &cgrp->impl, &cgrp->priv);
	if (ret)
		return ret;

	nvif_object_ctor(&device->object, "nvifCgrp", cgrp->impl->id,
			 device->impl->fifo.cgrp.oclass, &cgrp->object);
	return 0;
}
