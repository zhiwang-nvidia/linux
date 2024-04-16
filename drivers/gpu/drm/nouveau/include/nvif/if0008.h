#ifndef __NVIF_IF0008_H__
#define __NVIF_IF0008_H__

#define NVIF_MMU_V0_KIND                                                   0x02

struct nvif_mmu_kind_v0 {
	__u8  version;
	__u8  kind_inv;
	__u16 count;
	__u8  data[];
};
#endif
