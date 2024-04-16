/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_DRIVERIF_H__
#define __NVIF_DRIVERIF_H__
#include <drm/display/drm_dp.h>

struct nvif_event_priv;
struct nvif_client_priv;
struct nvif_device_priv;
struct nvif_control_priv;
struct nvif_usermode_priv;
struct nvif_mmu_priv;
struct nvif_mem_priv;
struct nvif_vmm_priv;
struct nvif_faultbuf_priv;
struct nvif_disp_priv;
struct nvif_disp_caps_priv;
struct nvif_conn_priv;
struct nvif_outp_priv;
struct nvif_head_priv;
struct nvif_disp_chan_priv;
struct nvif_ctxdma_priv;
struct nvif_cgrp_priv;
struct nvif_chan_priv;
struct nvif_engobj_priv;

struct nvif_event_impl {
	void (*del)(struct nvif_event_priv *);
	int (*allow)(struct nvif_event_priv *);
	int (*block)(struct nvif_event_priv *);
};

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

enum nvif_vmm_get_type {
	NVIF_VMM_GET_ADDR,
	NVIF_VMM_GET_PTES,
	NVIF_VMM_GET_LAZY,
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

	int (*get)(struct nvif_vmm_priv *, enum nvif_vmm_get_type, bool sparse, u8 page,
		   u8 align, u64 size, u64 *addr);
	int (*put)(struct nvif_vmm_priv *, u64 addr);

	int (*map)(struct nvif_vmm_priv *, u64 addr, u64 size, void *, u32,
		   struct nvif_mem_priv *, u64 offset);
	int (*unmap)(struct nvif_vmm_priv *, u64 addr);

#define NVIF_VMM_PFNMAP_ADDR       0xfffffffffffff000ULL
#define NVIF_VMM_PFNMAP_ADDR_SHIFT                    12
#define NVIF_VMM_PFNMAP_APER       0x00000000000000f0ULL
#define NVIF_VMM_PFNMAP_HOST       0x0000000000000000ULL
#define NVIF_VMM_PFNMAP_VRAM       0x0000000000000010ULL
#define NVIF_VMM_PFNMAP_A          0x0000000000000004ULL
#define NVIF_VMM_PFNMAP_W          0x0000000000000002ULL
#define NVIF_VMM_PFNMAP_V          0x0000000000000001ULL
#define NVIF_VMM_PFNMAP_NONE       0x0000000000000000ULL
	int (*pfnmap)(struct nvif_vmm_priv *, u8 page, u64 addr, u64 size, u64 *phys);
	int (*pfnclr)(struct nvif_vmm_priv *, u64 addr, u64 size);

	struct {
		int (*get)(struct nvif_vmm_priv *, u8 shift, u64 addr, u64 size);
		int (*put)(struct nvif_vmm_priv *, u8 shift, u64 addr, u64 size);
		int (*map)(struct nvif_vmm_priv *, u8 shift, u64 addr, u64 size, void *, u32,
			   struct nvif_mem_priv *, u64 offset);
		int (*unmap)(struct nvif_vmm_priv *, u8 shift, u64 addr, u64 size, bool sparse);
		int (*sparse)(struct nvif_vmm_priv *, u64 addr, u64 size, bool ref);
	} raw;

	struct {
		void (*replay)(struct nvif_vmm_priv *);
		void (*cancel)(struct nvif_vmm_priv *, u64 inst, u8 hub, u8 gpc, u8 client);
	} fault;
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
			   const struct nvif_mem_impl **, struct nvif_mem_priv **);
	} mem;

	struct {
		s32 oclass;
		int (*new)(struct nvif_mmu_priv *, enum nvif_vmm_type type, u64 addr, u64 size,
			   void *, u32, const struct nvif_vmm_impl **, struct nvif_vmm_priv **);
	} vmm;
};

struct nvif_faultbuf_impl {
	void (*del)(struct nvif_faultbuf_priv *);

	struct nvif_mapinfo map;
	u32 entries;
	u32 get;
	u32 put;

	struct {
		int (*new)(struct nvif_faultbuf_priv *, u64 token,
			   const struct nvif_event_impl **, struct nvif_event_priv **);
	} event;
};

struct nvif_disp_caps_impl {
	void (*del)(struct nvif_disp_caps_priv *);
	struct nvif_mapinfo map;
};

struct nvif_conn_impl {
	void (*del)(struct nvif_conn_priv *);

	enum nvif_conn_type {
		NVIF_CONN_VGA,
		NVIF_CONN_TV,
		NVIF_CONN_DVI_I,
		NVIF_CONN_DVI_D,
		NVIF_CONN_HDMI,
		NVIF_CONN_LVDS,
		NVIF_CONN_LVDS_SPWG,
		NVIF_CONN_DP,
		NVIF_CONN_EDP,
	} type;

	int (*event)(struct nvif_conn_priv *, u64 handle, u8 types,
		     const struct nvif_event_impl **, struct nvif_event_priv **);
};

enum nvif_outp_infoframe_type {
	NVIF_OUTP_INFOFRAME_AVI,
	NVIF_OUTP_INFOFRAME_VSI,
};

enum nvif_outp_detect_status {
	NVIF_OUTP_DETECT_NOT_PRESENT,
	NVIF_OUTP_DETECT_PRESENT,
	NVIF_OUTP_DETECT_UNKNOWN,
};

struct nvif_outp_dp_rate {
	int dpcd; /* -1 for non-indexed rates */
	u32 rate;
};

struct nvif_outp_impl {
	void (*del)(struct nvif_outp_priv *);

	u8 id;

	enum nvif_outp_type {
		NVIF_OUTP_DAC,
		NVIF_OUTP_SOR,
		NVIF_OUTP_PIOR,
	} type;

	enum nvif_outp_proto {
		NVIF_OUTP_RGB_CRT,
		NVIF_OUTP_TMDS,
		NVIF_OUTP_LVDS,
		NVIF_OUTP_DP,
	} proto;

	u8 heads;
#define NVIF_OUTP_DDC_INVALID 0xff
	u8 ddc;
	u8 conn;

	int (*detect)(struct nvif_outp_priv *, enum nvif_outp_detect_status *);
	int (*edid_get)(struct nvif_outp_priv *, u8 *data, u16 *size);

	int (*inherit)(struct nvif_outp_priv *, enum nvif_outp_proto,
		       u8 *or, u8 *link, u8 *head, u8 *proto_evo);
	int (*acquire)(struct nvif_outp_priv *, enum nvif_outp_type, bool hda, u8 *or, u8 *link);
	int (*release)(struct nvif_outp_priv *);

	int (*load_detect)(struct nvif_outp_priv *, u32 loadval, u8 *load);

	struct {
		u32 freq_max;
	} rgb_crt;

	struct {
		u8 dual;
	} tmds;

	struct {
		int (*config)(struct nvif_outp_priv *, u8 head, bool enable, u8 max_ac_packet,
			      u8 rekey, u32 khz, bool scdc, bool scdc_scrambling,
			      bool scdc_low_rates);
		int (*infoframe)(struct nvif_outp_priv *, u8 head, enum nvif_outp_infoframe_type,
				 u8 *data, u8 size);
	} hdmi;

	struct {
		int (*eld)(struct nvif_outp_priv *, u8 head, u8 *data, u8 size);
	} hda;

	struct {
		u8 acpi_edid;

		int (*config)(struct nvif_outp_priv *, bool dual, bool bpc8);
	} lvds;

	struct {
		int (*get)(struct nvif_outp_priv *, u8 *level);
		int (*set)(struct nvif_outp_priv *, u8 level);
	} bl;

	struct {
		u8 aux;
		u8 mst;
		u8 increased_wm;
		u8 link_nr;
		u32 link_bw;

		int (*aux_pwr)(struct nvif_outp_priv *, bool enable);
		int (*aux_xfer)(struct nvif_outp_priv *, u8 type, u32 addr, u8 *data, u8 *size);
		int (*rates)(struct nvif_outp_priv *, struct nvif_outp_dp_rate *, u8 rates);
		int (*train)(struct nvif_outp_priv *, u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 lttprs,
			     u8 link_nr, u32 link_bw, bool mst, bool post_lt_adj, bool retrain);
		int (*drive)(struct nvif_outp_priv *, u8 lanes, u8 pe[4], u8 vs[4]);
		int (*sst)(struct nvif_outp_priv *, u8 head,
			   u32 watermark, u32 hblanksym, u32 vblanksym);
		int (*mst_id_get)(struct nvif_outp_priv *, u32 *id);
		int (*mst_id_put)(struct nvif_outp_priv *, u32 id);
		int (*mst_vcpi)(struct nvif_outp_priv *, u8 head,
				u8 start_slot, u8 num_slots, u16 pbn, u16 aligned_pbn);
	} dp;
};

struct nvif_head_impl {
	void (*del)(struct nvif_head_priv *);

	int (*scanoutpos)(struct nvif_head_priv *, s64 time[2],
			  u16 *vblanks, u16 *vblanke, u16 *vtotal, u16 *vline,
			  u16 *hblanks, u16 *hblanke, u16 *htotal, u16 *hline);

	int (*vblank)(struct nvif_head_priv *, u64 handle,
		      const struct nvif_event_impl **, struct nvif_event_priv **);
};

#include <nvif/cl0002.h>

struct nvif_ctxdma_impl {
	void (*del)(struct nvif_ctxdma_priv *);
};

struct nvif_disp_chan_impl {
	void (*del)(struct nvif_disp_chan_priv *);
	struct nvif_mapinfo map;

	struct {
		int (*new)(struct nvif_disp_chan_priv *, u32 handle, s32 oclass,
			   struct nv_dma_v0 *argv, u32 argc,
			   const struct nvif_ctxdma_impl **, struct nvif_ctxdma_priv **);
	} ctxdma;
};

struct nvif_disp_impl {
	void (*del)(struct nvif_disp_priv *);

	struct {
		u32 oclass;
		int (*new)(struct nvif_disp_priv *,
			   const struct nvif_disp_caps_impl **, struct nvif_disp_caps_priv **);
	} caps;

	struct {
		u32 mask;
		int (*new)(struct nvif_disp_priv *, u8 id,
			   const struct nvif_conn_impl **, struct nvif_conn_priv **);
	} conn;

	struct {
		u32 mask;
		int (*new)(struct nvif_disp_priv *, u8 id,
			   const struct nvif_outp_impl **, struct nvif_outp_priv **);
	} outp;

	struct {
		u32 mask;
		int (*new)(struct nvif_disp_priv *, u8 id,
			   const struct nvif_head_impl **, struct nvif_head_priv **);
	} head;

	struct {
		struct nvif_disp_impl_core {
			s32 oclass;
			int (*new)(struct nvif_disp_priv *, struct nvif_mem_priv *,
				   const struct nvif_disp_chan_impl **,
				   struct nvif_disp_chan_priv **);
		} core;

		struct nvif_disp_impl_dmac {
			s32 oclass;
			int (*new)(struct nvif_disp_priv *, u8 id, struct nvif_mem_priv *,
				   const struct nvif_disp_chan_impl **,
				   struct nvif_disp_chan_priv **);
		} base, ovly, wndw, wimm;

		struct nvif_disp_impl_pioc {
			s32 oclass;
			int (*new)(struct nvif_disp_priv *, u8 id,
				   const struct nvif_disp_chan_impl **,
				   struct nvif_disp_chan_priv **);
		} curs, oimm;
	} chan;
};

struct nvif_engobj_impl {
	void (*del)(struct nvif_engobj_priv *);
};

struct nvif_chan_impl {
	void (*del)(struct nvif_chan_priv *);

	u16 id;
	u32 doorbell_token;

	struct {
		enum {
			NVIF_CHAN_INST_APER_INST,
			NVIF_CHAN_INST_APER_VRAM,
			NVIF_CHAN_INST_APER_HOST,
			NVIF_CHAN_INST_APER_NCOH,
		} aper;
		u64 addr;
	} inst;

	struct nvif_mapinfo map;

	struct {
		int (*killed)(struct nvif_chan_priv *, u64 token,
			      const struct nvif_event_impl **, struct nvif_event_priv **);
		int (*nonstall)(struct nvif_chan_priv *, u64 token,
				const struct nvif_event_impl **, struct nvif_event_priv **);
	} event;

	struct {
		int (*new)(struct nvif_chan_priv *, u32 handle, s32 oclass, void *argv, u32 argc,
			   const struct nvif_ctxdma_impl **, struct nvif_ctxdma_priv **);
	} ctxdma;

	struct {
		int (*new)(struct nvif_chan_priv *, u32 handle, u8 engi, s32 oclass,
			   const struct nvif_engobj_impl **, struct nvif_engobj_priv **,
			   u64 object);
	} engobj;
};

struct nvif_cgrp_impl {
	void (*del)(struct nvif_cgrp_priv *);

	u16 id;

	struct {
		int (*new)(struct nvif_cgrp_priv *, u8 runq, bool priv, u16 devm,
			   u64 gpfifo_offset, u64 gpfifo_length,
			   struct nvif_mem_priv *userd, u16 userd_offset, const char *name,
			   const struct nvif_chan_impl **, struct nvif_chan_priv **);
	} chan;
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
		int (*new)(struct nvif_device_priv *,
			   const struct nvif_faultbuf_impl **, struct nvif_faultbuf_priv **);
	} faultbuf;

	struct {
		s32 oclass;
		int (*new)(struct nvif_device_priv *,
			   const struct nvif_disp_impl **, struct nvif_disp_priv **);
	} disp;

	struct {
		int (*new)(struct nvif_device_priv *, s32 oclass, void *argv, u32 argc,
			   const struct nvif_ctxdma_impl **, struct nvif_ctxdma_priv **);
	} ctxdma;

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
			int (*new)(struct nvif_device_priv *, u8 runl, struct nvif_vmm_priv *,
				   const char *name, const struct nvif_cgrp_impl **,
				   struct nvif_cgrp_priv **);
		} cgrp;

		struct {
			s32 oclass;
			int (*new)(struct nvif_device_priv *, u8 runl, u8 runq, bool priv, u16 devm,
				   struct nvif_vmm_priv *, struct nvif_ctxdma_priv *push,
				   u64 offset, u64 length, struct nvif_mem_priv *userd,
				   u16 userd_offset, const char *name,
				   const struct nvif_chan_impl **, struct nvif_chan_priv **,
				   u64 handle);
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
			   const struct nvif_device_impl **, struct nvif_device_priv **);
	} device;
};
#endif
