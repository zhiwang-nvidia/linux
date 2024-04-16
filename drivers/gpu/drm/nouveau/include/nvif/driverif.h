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

	enum {
		NVIF_DEVICE_IGP = 0,
		NVIF_DEVICE_PCI,
		NVIF_DEVICE_AGP,
		NVIF_DEVICE_PCIE,
		NVIF_DEVICE_SOC,
	} platform;

	u16 chipset; /* from NV_PMC_BOOT_0 */
	u8 revision; /* from NV_PMC_BOOT_0 */

	enum {
		NVIF_DEVICE_TNT = 1,
		NVIF_DEVICE_CELSIUS,
		NVIF_DEVICE_KELVIN,
		NVIF_DEVICE_RANKINE,
		NVIF_DEVICE_CURIE,
		NVIF_DEVICE_TESLA,
		NVIF_DEVICE_FERMI,
		NVIF_DEVICE_KEPLER,
		NVIF_DEVICE_MAXWELL,
		NVIF_DEVICE_PASCAL,
		NVIF_DEVICE_VOLTA,
		NVIF_DEVICE_TURING,
		NVIF_DEVICE_AMPERE,
		NVIF_DEVICE_ADA,
	} family;

	char chip[16];
	char name[64];

	u64 ram_size;
	u64 ram_user;

	u64 (*time)(struct nvif_device_priv *);

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
		u8  engine_nr;
		u8  runl_nr;
		u16 chan_nr; /* 0 == per-runlist */

		struct nvif_device_impl_engine {
			enum nvif_engine_type {
				NVIF_ENGINE_SW,
				NVIF_ENGINE_GR,
				NVIF_ENGINE_MPEG,
				NVIF_ENGINE_ME,
				NVIF_ENGINE_CIPHER,
				NVIF_ENGINE_BSP,
				NVIF_ENGINE_VP,
				NVIF_ENGINE_CE,
				NVIF_ENGINE_SEC,
				NVIF_ENGINE_MSVLD,
				NVIF_ENGINE_MSPDEC,
				NVIF_ENGINE_MSPPP,
				NVIF_ENGINE_MSENC,
				NVIF_ENGINE_VIC,
				NVIF_ENGINE_SEC2,
				NVIF_ENGINE_NVDEC,
				NVIF_ENGINE_NVENC,
				NVIF_ENGINE_NVJPG,
				NVIF_ENGINE_OFA,
			} type;

			u8  oclass_nr;
			s32 oclass[64];
		} engine[8];

		struct nvif_device_impl_runl {
			u8  id;
			u16 chan_nr;
			u8  runq_nr;
			u8  engn_nr;

			struct {
				u8 engine;
				u8 inst;
			} engn[8];
		} runl[64];

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
