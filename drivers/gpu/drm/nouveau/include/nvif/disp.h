#ifndef __NVIF_DISP_H__
#define __NVIF_DISP_H__
#include <nvif/object.h>
struct nvif_device;

struct nvif_disp {
	const struct nvif_disp_impl *impl;
	struct nvif_disp_priv *priv;
	struct nvif_object object;

	struct nvif_device *device;

	unsigned long conn_mask;
	unsigned long outp_mask;
	unsigned long head_mask;
};

int nvif_disp_ctor(struct nvif_device *, const char *name, struct nvif_disp *);
void nvif_disp_dtor(struct nvif_disp *);
#endif
