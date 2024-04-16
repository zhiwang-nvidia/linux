/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CGRP_H__
#define __NVIF_CGRP_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
struct nvif_device;
struct nvif_vmm;

struct nvif_cgrp {
	const struct nvif_cgrp_impl *impl;
	struct nvif_cgrp_priv *priv;
	struct nvif_object object;
};

int nvif_cgrp_ctor(struct nvif_device *, struct nvif_vmm *, int runl, struct nvif_cgrp *);
void nvif_cgrp_dtor(struct nvif_cgrp *);
#endif
