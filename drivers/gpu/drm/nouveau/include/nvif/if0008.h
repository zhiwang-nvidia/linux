#ifndef __NVIF_IF0008_H__
#define __NVIF_IF0008_H__

#define NVIF_MMU_V0_TYPE                                                   0x01
#define NVIF_MMU_V0_KIND                                                   0x02

struct nvif_mmu_type_v0 {
	__u8  version;
	__u8  index;
	__u8  heap;
	__u8  vram;
	__u8  host;
	__u8  comp;
	__u8  disp;
	__u8  kind;
	__u8  mappable;
	__u8  coherent;
	__u8  uncached;
};

struct nvif_mmu_kind_v0 {
	__u8  version;
	__u8  kind_inv;
	__u16 count;
	__u8  data[];
};
#endif
