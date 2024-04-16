/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "ctrl.h"

#include <core/client.h>
#include <subdev/clk.h>

struct nvif_control_priv {
	struct nvkm_object object;
	struct nvkm_device *device;
};

static void
nvkm_control_pstate_info(struct nvif_control_priv *ctrl, struct nvif_control_pstate_info *info)
{
	struct nvkm_clk *clk = ctrl->device->clk;

	if (clk) {
		info->count = clk->state_nr;
		info->ustate_ac = clk->ustate_ac;
		info->ustate_dc = clk->ustate_dc;
		info->pwrsrc = clk->pwrsrc;
		info->pstate = clk->pstate;
	} else {
		info->count = 0;
		info->ustate_ac = NVIF_CONTROL_PSTATE_INFO_USTATE_DISABLE;
		info->ustate_dc = NVIF_CONTROL_PSTATE_INFO_USTATE_DISABLE;
		info->pwrsrc = -ENODEV;
		info->pstate = NVIF_CONTROL_PSTATE_INFO_PSTATE_UNKNOWN;
	}
}

static int
nvkm_control_pstate_attr(struct nvif_control_priv *ctrl, struct nvif_control_pstate_attr *attr)
{
	struct nvkm_clk *clk = ctrl->device->clk;
	const struct nvkm_domain *domain;
	struct nvkm_pstate *pstate;
	struct nvkm_cstate *cstate;
	int i = 0, j = -1;
	u32 lo, hi;

	if (!clk)
		return -ENODEV;
	if (attr->state < NVIF_CONTROL_PSTATE_ATTR_STATE_CURRENT)
		return -EINVAL;
	if (attr->state >= clk->state_nr)
		return -EINVAL;

	domain = clk->domains;

	while (domain->name != nv_clk_src_max) {
		if (domain->mname && ++j == attr->index)
			break;
		domain++;
	}

	if (domain->name == nv_clk_src_max)
		return -EINVAL;

	if (attr->state != NVIF_CONTROL_PSTATE_ATTR_STATE_CURRENT) {
		list_for_each_entry(pstate, &clk->states, head) {
			if (i++ == attr->state)
				break;
		}

		lo = pstate->base.domain[domain->name];
		hi = lo;
		list_for_each_entry(cstate, &pstate->list, head) {
			lo = min(lo, cstate->domain[domain->name]);
			hi = max(hi, cstate->domain[domain->name]);
		}

		attr->state = pstate->pstate;
	} else {
		lo = max(nvkm_clk_read(clk, domain->name), 0);
		hi = lo;
	}

	snprintf(attr->name, sizeof(attr->name), "%s", domain->mname);
	snprintf(attr->unit, sizeof(attr->unit), "MHz");
	attr->min = lo / domain->mdiv;
	attr->max = hi / domain->mdiv;

	attr->index = 0;
	while ((++domain)->name != nv_clk_src_max) {
		if (domain->mname) {
			attr->index = ++j;
			break;
		}
	}

	return 0;
}

static int
nvkm_control_pstate_user(struct nvif_control_priv *ctrl, struct nvif_control_pstate_user *user)
{
	struct nvkm_clk *clk = ctrl->device->clk;
	int ret = 0;

	if (!clk)
		return -ENODEV;

	if (user->pwrsrc >= 0) {
		ret |= nvkm_clk_ustate(clk, user->ustate, user->pwrsrc);
	} else {
		ret |= nvkm_clk_ustate(clk, user->ustate, 0);
		ret |= nvkm_clk_ustate(clk, user->ustate, 1);
	}

	return ret;
}

static const struct nvkm_object_func
nvkm_control = {
};

static void
nvkm_control_del(struct nvif_control_priv *ctrl)
{
	struct nvkm_object *object = &ctrl->object;

	nvkm_object_del(&object);
}

static const struct nvif_control_impl
nvkm_control_impl = {
	.del = nvkm_control_del,
	.pstate.info = nvkm_control_pstate_info,
	.pstate.attr = nvkm_control_pstate_attr,
	.pstate.user = nvkm_control_pstate_user,
};

int
nvkm_control_new(struct nvkm_device *device, const struct nvif_control_impl **pimpl,
		 struct nvif_control_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvif_control_priv *ctrl;

	if (!(ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_control, &(struct nvkm_oclass) {}, &ctrl->object);
	ctrl->device = device;

	*pimpl = &nvkm_control_impl;
	*ppriv = ctrl;
	*pobject = &ctrl->object;
	return 0;
}
