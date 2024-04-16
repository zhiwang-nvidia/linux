/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DRIVERIF_H__
#define __NVIF_DRIVERIF_H__
struct nvif_client_priv;
struct nvif_device_priv;

struct nvif_driver {
	const char *name;
	int (*suspend)(struct nvif_client_priv *);
	int (*resume)(struct nvif_client_priv *);
	int (*ioctl)(void *priv, void *data, u32 size, void **hack);
	void __iomem *(*map)(struct nvif_client_priv *, u64 handle, u32 size);
	void (*unmap)(struct nvif_client_priv *, void __iomem *ptr, u32 size);
};

struct nvif_mapinfo {
	enum nvif_map_type {
		NVIF_MAP_IO,
		NVIF_MAP_VA,
	} type;
	u64 handle;
	u64 length;
};

struct nvif_device_impl {
	void (*del)(struct nvif_device_priv *);

	struct nvif_mapinfo map;

	struct {
		s32 oclass;
	} usermode;

	struct {
		s32 oclass;
	} mmu;

	struct {
		s32 oclass;
	} faultbuf;

	struct {
		s32 oclass;
	} disp;

	struct nvif_device_impl_fifo {
		struct {
			s32 oclass;
		} cgrp;

		struct {
			s32 oclass;
		} chan;
	} fifo;
};

struct nvif_client_impl {
	void (*del)(struct nvif_client_priv *);

	struct {
		int (*new)(struct nvif_client_priv *parent,
			   const struct nvif_client_impl **, struct nvif_client_priv **);
	} client;

	struct {
		int (*new)(struct nvif_client_priv *,
			   const struct nvif_device_impl **, struct nvif_device_priv **,
			   u64 handle);
	} device;
};
#endif
