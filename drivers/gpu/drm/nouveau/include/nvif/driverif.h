/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DRIVERIF_H__
#define __NVIF_DRIVERIF_H__
struct nvif_client_priv;
struct nvif_device_priv;
struct nvif_control_priv;
struct nvif_usermode_priv;
struct nvif_mmu_priv;
struct nvif_mem_priv;
struct nvif_vmm_priv;

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

struct nvif_control_pstate_user {
	u8  version;
#define NVIF_CONTROL_PSTATE_USER_STATE_UNKNOWN                          (-1)
#define NVIF_CONTROL_PSTATE_USER_STATE_PERFMON                          (-2)
	s8  ustate; /*  in: pstate identifier */
	s8  pwrsrc; /*  in: target power source */
};

struct nvif_control_pstate_attr {
	u8  version;
#define NVIF_CONTROL_PSTATE_ATTR_STATE_CURRENT                          (-1)
	s8  state; /*  in: index of pstate to query
		    * out: pstate identifier
		    */
	u8  index; /*  in: index of attribute to query
		    * out: index of next attribute, or 0 if no more
		    */
	u32 min;
	u32 max;
	char  name[32];
	char  unit[16];
};

struct nvif_control_pstate_info {
	u8  version;
	u8  count; /* out: number of power states */
#define NVIF_CONTROL_PSTATE_INFO_USTATE_DISABLE                         (-1)
#define NVIF_CONTROL_PSTATE_INFO_USTATE_PERFMON                         (-2)
	s8  ustate_ac; /* out: target pstate index */
	s8  ustate_dc; /* out: target pstate index */
	s8  pwrsrc; /* out: current power source */
#define NVIF_CONTROL_PSTATE_INFO_PSTATE_UNKNOWN                         (-1)
#define NVIF_CONTROL_PSTATE_INFO_PSTATE_PERFMON                         (-2)
	s8  pstate; /* out: current pstate index */
};

struct nvif_control_impl {
	void (*del)(struct nvif_control_priv *);

	struct {
		void (*info)(struct nvif_control_priv *, struct nvif_control_pstate_info *);
		int  (*attr)(struct nvif_control_priv *, struct nvif_control_pstate_attr *);
		int  (*user)(struct nvif_control_priv *, struct nvif_control_pstate_user *);
	} pstate;
};

struct nvif_usermode_impl {
	void (*del)(struct nvif_usermode_priv *);
	struct nvif_mapinfo map;
};

struct nvif_mem_impl {
	void (*del)(struct nvif_mem_priv *);

	u8 page;
	u64 addr;
	u64 size;

	int (*map)(struct nvif_mem_priv *, void *argv, u32 argc, struct nvif_mapinfo *);
	int (*unmap)(struct nvif_mem_priv *);
};

enum nvif_vmm_type {
	NVIF_VMM_TYPE_UNMANAGED,
	NVIF_VMM_TYPE_MANAGED,
	NVIF_VMM_TYPE_RAW,
};

struct nvif_vmm_impl {
	void (*del)(struct nvif_vmm_priv *);

	u64 start;
	u64 limit;
	u8 page_nr;

	struct {
		u8 shift;
		bool sparse;
		bool vram;
		bool host;
		bool comp;
	} page[8];
};

struct nvif_mmu_impl {
	void (*del)(struct nvif_mmu_priv *);

	u8 dmabits;
	u8 heap_nr;
	u8 type_nr;

	u8 kind_inv;
	u16 kind_nr;

	struct {
		u64 size;
	} heap[4];

	struct {
#define NVIF_MEM_VRAM     0x01
#define NVIF_MEM_HOST     0x02
#define NVIF_MEM_COMP     0x04
#define NVIF_MEM_DISP     0x08
#define NVIF_MEM_KIND     0x10
#define NVIF_MEM_MAPPABLE 0x20
#define NVIF_MEM_COHERENT 0x40
#define NVIF_MEM_UNCACHED 0x80
		u8 type;
		u8 heap;
	} type[16];

	const u8 *kind;

	struct {
		s32 oclass;
		int (*new)(struct nvif_mmu_priv *, u8 type, u8 page, u64 size, void *argv, u32 argc,
			   const struct nvif_mem_impl **, struct nvif_mem_priv **, u64 handle);
	} mem;

	struct {
		s32 oclass;
		int (*new)(struct nvif_mmu_priv *, enum nvif_vmm_type type, u64 addr, u64 size,
			   void *, u32, const struct nvif_vmm_impl **, struct nvif_vmm_priv **,
			   u64 handle);
	} vmm;
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
		int (*new)(struct nvif_device_priv *,
			   const struct nvif_control_impl **, struct nvif_control_priv **);
	} control;

	struct {
		s32 oclass;
		int (*new)(struct nvif_device_priv *,
			   const struct nvif_usermode_impl **, struct nvif_usermode_priv **);
	} usermode;

	struct {
		s32 oclass;
		int (*new)(struct nvif_device_priv *, const struct nvif_mmu_impl **,
			   struct nvif_mmu_priv **);
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
