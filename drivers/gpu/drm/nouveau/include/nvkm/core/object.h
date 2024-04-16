/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_OBJECT_H__
#define __NVKM_OBJECT_H__
#include <core/oclass.h>
struct nvkm_event;
struct nvkm_gpuobj;
struct nvif_event_impl;
struct nvif_event_priv;

struct nvkm_object {
	const struct nvkm_object_func *func;
	struct nvkm_client *client;
	struct nvkm_engine *engine;
	s32 oclass;
	u32 handle;

	struct list_head head;
	struct list_head tree;
	u64 object;
	struct rb_node node;
};

struct nvkm_object_func {
	void *(*dtor)(struct nvkm_object *);
	int (*init)(struct nvkm_object *);
	int (*fini)(struct nvkm_object *, bool suspend);
	int (*mthd)(struct nvkm_object *, u32 mthd, void *data, u32 size);
	int (*ntfy)(struct nvkm_object *, u32 mthd, struct nvkm_event **);
	int (*bind)(struct nvkm_object *, struct nvkm_gpuobj *, int align,
		    struct nvkm_gpuobj **);
	int (*sclass)(struct nvkm_object *, int index, struct nvkm_oclass *);
	int (*uevent)(struct nvkm_object *, u64 token,
		      const struct nvif_event_impl **, struct nvif_event_priv **);
};

void nvkm_object_ctor(const struct nvkm_object_func *,
		      const struct nvkm_oclass *, struct nvkm_object *);
int nvkm_object_new_(const struct nvkm_object_func *,
		     const struct nvkm_oclass *, void *data, u32 size,
		     struct nvkm_object **);
int nvkm_object_new(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
void nvkm_object_del(struct nvkm_object **);
void *nvkm_object_dtor(struct nvkm_object *);
int nvkm_object_init(struct nvkm_object *);
int nvkm_object_fini(struct nvkm_object *, bool suspend);
int nvkm_object_mthd(struct nvkm_object *, u32 mthd, void *data, u32 size);
int nvkm_object_ntfy(struct nvkm_object *, u32 mthd, struct nvkm_event **);
int nvkm_object_bind(struct nvkm_object *, struct nvkm_gpuobj *, int align,
		     struct nvkm_gpuobj **);

void nvkm_object_link_(struct nvif_client_priv *, struct nvkm_object *parent, struct nvkm_object *);
int nvkm_object_link_rb(struct nvif_client_priv *, struct nvkm_object *parent, u64 handle,
		        struct nvkm_object *);

static inline void
nvkm_object_link(struct nvkm_object *parent, struct nvkm_object *object)
{
	nvkm_object_link_(parent->client, parent, object);
}

bool nvkm_object_insert(struct nvkm_object *);
void nvkm_object_remove(struct nvkm_object *);
struct nvkm_object *nvkm_object_search(struct nvkm_client *, u64 object,
				       const struct nvkm_object_func *);
#endif
