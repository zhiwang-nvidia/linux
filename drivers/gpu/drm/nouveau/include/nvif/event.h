/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_EVENT_H__
#define __NVIF_EVENT_H__
#include <nvif/object.h>
#include <nvif/driverif.h>
struct nvif_event;

enum nvif_event_stat nvif_event(u64 token, void *repv, u32 repc);

typedef enum nvif_event_stat (*nvif_event_func)(struct nvif_event *, void *repv, u32 repc);

struct nvif_event {
	const struct nvif_event_impl *impl;
	struct nvif_event_priv *priv;
	struct nvif_object object;

	nvif_event_func func;
};

static inline bool
nvif_event_constructed(struct nvif_event *event)
{
	return nvif_object_constructed(&event->object);
}

static inline void
nvif_event_ctor(struct nvif_object *parent, const char *name, u32 handle,
		nvif_event_func func, struct nvif_event *event)
{
	nvif_object_ctor(parent, name ?: "nvifEvent", handle, 0, &event->object);
	event->func = func;
}

void nvif_event_dtor(struct nvif_event *);
int nvif_event_allow(struct nvif_event *);
int nvif_event_block(struct nvif_event *);
#endif
