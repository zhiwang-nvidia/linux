/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CONN_H__
#define __NVIF_CONN_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
#include <nvif/event.h>
struct nvif_disp;

struct nvif_conn {
	const struct nvif_conn_impl *impl;
	struct nvif_conn_priv *priv;
	struct nvif_object object;

	u32 id;
};

int nvif_conn_ctor(struct nvif_disp *, const char *name, int id, struct nvif_conn *);
void nvif_conn_dtor(struct nvif_conn *);

static inline int
nvif_conn_id(struct nvif_conn *conn)
{
	return conn->object.handle;
}

int nvif_conn_event_ctor(struct nvif_conn *, const char *name, nvif_event_func, u8 types,
			 struct nvif_event *);
#endif
