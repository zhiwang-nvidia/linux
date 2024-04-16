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
#include "user.h"
#include "priv.h"
#include "ctrl.h"

#include <core/client.h>
#include <subdev/fault/user.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>
#include <subdev/timer.h>
#include <subdev/mmu/ummu.h>
#include <subdev/vfn/uvfn.h>
#include <engine/disp/priv.h>
#include <engine/disp/udisp.h>
#include <engine/fifo/ufifo.h>
#include <engine/fifo/ucgrp.h>

struct nvif_device_priv {
	struct nvkm_object object;
	struct nvkm_device *device;

	struct nvif_device_impl impl;
};

static int
nvkm_udevice_fault_new(struct nvif_device_priv *udev,
		       const struct nvif_faultbuf_impl **pimpl, struct nvif_faultbuf_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_ufault_new(udev->device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static int
nvkm_udevice_usermode_new(struct nvif_device_priv *udev, const struct nvif_usermode_impl **pimpl,
			  struct nvif_usermode_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_uvfn_new(udev->device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static int
nvkm_udevice_control_new(struct nvif_device_priv *udev,
			 const struct nvif_control_impl **pimpl, struct nvif_control_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_control_new(udev->device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static u64
nvkm_udevice_time(struct nvif_device_priv *udev)
{
	return nvkm_timer_read(udev->device->timer);
}

static int
nvkm_udevice_cgrp_new(struct nvif_device_priv *udev, u8 runl, struct nvif_vmm_priv *uvmm,
		      const char *name, const struct nvif_cgrp_impl **pimpl,
		      struct nvif_cgrp_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_ucgrp_new(udev->device->fifo, runl, uvmm, name, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static int
nvkm_udevice_disp_new(struct nvif_device_priv *udev,
		      const struct nvif_disp_impl **pimpl, struct nvif_disp_priv **ppriv)
{
	struct nvkm_object *object;
	int ret;

	ret = nvkm_udisp_new(udev->device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static int
nvkm_udevice_mmu_new(struct nvif_device_priv *udev,
		     const struct nvif_mmu_impl **pimpl, struct nvif_mmu_priv **ppriv)
{
	struct nvkm_device *device = udev->device;
	struct nvkm_object *object;
	int ret;

	ret = nvkm_ummu_new(device, pimpl, ppriv, &object);
	if (ret)
		return ret;

	nvkm_object_link(&udev->object, object);
	return 0;
}

static void
nvkm_udevice_del(struct nvif_device_priv *udev)
{
	struct nvkm_object *object = &udev->object;

	nvkm_object_fini(object, false);
	nvkm_object_del(&object);
}

static const struct nvif_device_impl
nvkm_udevice_impl = {
	.del = nvkm_udevice_del,
	.time = nvkm_udevice_time,
	.control.new = nvkm_udevice_control_new,
	.usermode.new = nvkm_udevice_usermode_new,
};

static int
nvkm_udevice_fini(struct nvkm_object *object, bool suspend)
{
	struct nvif_device_priv *udev = container_of(object, typeof(*udev), object);
	struct nvkm_device *device = udev->device;
	int ret = 0;

	mutex_lock(&device->mutex);
	if (!--device->refcount) {
		ret = nvkm_device_fini(device, suspend);
		if (ret && suspend) {
			device->refcount++;
			goto done;
		}
	}

done:
	mutex_unlock(&device->mutex);
	return ret;
}

static int
nvkm_udevice_init(struct nvkm_object *object)
{
	struct nvif_device_priv *udev = container_of(object, typeof(*udev), object);
	struct nvkm_device *device = udev->device;
	int ret = 0;

	mutex_lock(&device->mutex);
	if (!device->refcount++) {
		ret = nvkm_device_init(device);
		if (ret) {
			device->refcount--;
			goto done;
		}
	}

done:
	mutex_unlock(&device->mutex);
	return ret;
}

static int
nvkm_udevice_child_new(const struct nvkm_oclass *oclass,
		       void *data, u32 size, struct nvkm_object **pobject)
{
	struct nvif_device_priv *udev = container_of(oclass->parent, typeof(*udev), object);
	const struct nvkm_device_oclass *sclass = oclass->priv;
	return sclass->ctor(udev->device, oclass, data, size, pobject);
}

static int
nvkm_udevice_child_get(struct nvkm_object *object, int index,
		       struct nvkm_oclass *oclass)
{
	struct nvif_device_priv *udev = container_of(object, typeof(*udev), object);
	struct nvkm_device *device = udev->device;
	struct nvkm_engine *engine;
	u64 mask = (1ULL << NVKM_ENGINE_DMAOBJ) |
		   (1ULL << NVKM_ENGINE_FIFO);
	const struct nvkm_device_oclass *sclass = NULL;
	int i;

	for (; i = __ffs64(mask), mask && !sclass; mask &= ~(1ULL << i)) {
		if (!(engine = nvkm_device_engine(device, i, 0)) ||
		    !(engine->func->base.sclass))
			continue;
		oclass->engine = engine;

		index -= engine->func->base.sclass(oclass, index, &sclass);
	}

	if (!sclass)
		return -EINVAL;

	oclass->ctor = nvkm_udevice_child_new;
	oclass->priv = sclass;
	return 0;
}

static const struct nvkm_object_func
nvkm_udevice = {
	.init = nvkm_udevice_init,
	.fini = nvkm_udevice_fini,
	.sclass = nvkm_udevice_child_get,
};

int
nvkm_udevice_new(struct nvkm_device *device,
		 const struct nvif_device_impl **pimpl, struct nvif_device_priv **ppriv,
		 struct nvkm_object **pobject)
{
	struct nvif_device_priv *udev;
	int ret;

	if (!(udev = kzalloc(sizeof(*udev), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_udevice, &(struct nvkm_oclass) {}, &udev->object);
	udev->device = device;

	ret = nvkm_udevice_init(&udev->object);
	if (ret) {
		kfree(udev);
		return ret;
	}

	udev->impl = nvkm_udevice_impl;
	udev->impl.map.type = NVIF_MAP_IO;
	udev->impl.map.handle = device->func->resource_addr(device, 0);
	udev->impl.map.length = device->func->resource_size(device, 0);

	switch (device->chipset) {
	case 0x01a:
	case 0x01f:
	case 0x04c:
	case 0x04e:
	case 0x063:
	case 0x067:
	case 0x068:
	case 0x0aa:
	case 0x0ac:
	case 0x0af:
		udev->impl.platform = NVIF_DEVICE_IGP;
		break;
	default:
		switch (device->type) {
		case NVKM_DEVICE_PCI  : udev->impl.platform = NVIF_DEVICE_PCI; break;
		case NVKM_DEVICE_AGP  : udev->impl.platform = NVIF_DEVICE_AGP; break;
		case NVKM_DEVICE_PCIE : udev->impl.platform = NVIF_DEVICE_PCIE; break;
		case NVKM_DEVICE_TEGRA: udev->impl.platform = NVIF_DEVICE_SOC; break;
		default:
			WARN_ON(1);
			ret = -EINVAL;
			goto done;
		}
		break;
	}

	udev->impl.chipset  = device->chipset;
	udev->impl.revision = device->chiprev;

	switch (device->card_type) {
	case NV_04: udev->impl.family = NVIF_DEVICE_TNT; break;
	case NV_10:
	case NV_11: udev->impl.family = NVIF_DEVICE_CELSIUS; break;
	case NV_20: udev->impl.family = NVIF_DEVICE_KELVIN; break;
	case NV_30: udev->impl.family = NVIF_DEVICE_RANKINE; break;
	case NV_40: udev->impl.family = NVIF_DEVICE_CURIE; break;
	case NV_50: udev->impl.family = NVIF_DEVICE_TESLA; break;
	case NV_C0: udev->impl.family = NVIF_DEVICE_FERMI; break;
	case NV_E0: udev->impl.family = NVIF_DEVICE_KEPLER; break;
	case GM100: udev->impl.family = NVIF_DEVICE_MAXWELL; break;
	case GP100: udev->impl.family = NVIF_DEVICE_PASCAL; break;
	case GV100: udev->impl.family = NVIF_DEVICE_VOLTA; break;
	case TU100: udev->impl.family = NVIF_DEVICE_TURING; break;
	case GA100: udev->impl.family = NVIF_DEVICE_AMPERE; break;
	case AD100: udev->impl.family = NVIF_DEVICE_ADA; break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
		goto done;
	}

	snprintf(udev->impl.chip, sizeof(udev->impl.chip), "%s", device->chip->name);
	snprintf(udev->impl.name, sizeof(udev->impl.name), "%s", device->name);

	if (device->fb && device->fb->ram)
		udev->impl.ram_size = udev->impl.ram_user = device->fb->ram->size;
	else
		udev->impl.ram_size = udev->impl.ram_user = 0;

	if (device->imem && udev->impl.ram_size > 0)
		udev->impl.ram_user = udev->impl.ram_user - device->imem->reserved;

	if (device->vfn) {
		udev->impl.usermode.oclass = device->vfn->user.base.oclass;
		udev->impl.usermode.new = nvkm_udevice_usermode_new;
	}

	if (device->mmu) {
		udev->impl.mmu.oclass = device->mmu->user.base.oclass;
		udev->impl.mmu.new = nvkm_udevice_mmu_new;
	}

	if (device->fault) {
		udev->impl.faultbuf.oclass = device->fault->user.base.oclass;
		udev->impl.faultbuf.new = nvkm_udevice_fault_new;
	}

	if (device->disp) {
		udev->impl.disp.oclass = device->disp->func->user.root.oclass;
		udev->impl.disp.new = nvkm_udevice_disp_new;
	}

	if (device->fifo) {
		if (!WARN_ON(nvkm_subdev_oneinit(&device->fifo->engine.subdev))) {
			nvkm_ufifo_ctor(device->fifo, &udev->impl.fifo);

			udev->impl.fifo.cgrp.new = nvkm_udevice_cgrp_new;
		}
	}

	*pimpl = &udev->impl;
	*ppriv = udev;
	*pobject = &udev->object;

done:
	if (ret)
		nvkm_udevice_del(udev);

	return ret;
}
