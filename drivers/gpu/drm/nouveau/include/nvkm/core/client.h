/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CLIENT_H__
#define __NVKM_CLIENT_H__
#include <core/object.h>

#include <nvif/driverif.h>

struct nvkm_client {
	struct nvkm_object object;
	char name[32];
	struct nvkm_device *device;
	u32 debug;

	spinlock_t obj_lock;

	void *data;
	int (*event)(u64 token, void *argv, u32 argc);
};

int nvkm_client_new(const char *name, struct nvkm_device *, int (*event)(u64, void *, u32),
		    const struct nvif_client_impl **, struct nvif_client_priv **);
int nvkm_client_event(struct nvkm_client *client, u64 token, void *repv, u32 repc);

/* logging for client-facing objects */
#define nvif_printk(o,l,p,f,a...) do {                                         \
	const struct nvkm_object *_object = (o);                               \
	const struct nvkm_client *_client = _object->client;                   \
	if (_client->debug >= NV_DBG_##l)                                      \
		printk(KERN_##p "nouveau: %s:%08x:%08x: "f, _client->name,     \
		       _object->handle, _object->oclass, ##a);                 \
} while(0)
#define nvif_fatal(o,f,a...) nvif_printk((o), FATAL, CRIT, f, ##a)
#define nvif_error(o,f,a...) nvif_printk((o), ERROR,  ERR, f, ##a)
#define nvif_debug(o,f,a...) nvif_printk((o), DEBUG, INFO, f, ##a)
#define nvif_trace(o,f,a...) nvif_printk((o), TRACE, INFO, f, ##a)
#define nvif_info(o,f,a...)  nvif_printk((o),  INFO, INFO, f, ##a)
#endif
