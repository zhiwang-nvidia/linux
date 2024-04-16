#ifndef __NVIF_IF000C_H__
#define __NVIF_IF000C_H__

#define NVIF_VMM_V0_PFNMAP                                                 0x05
#define NVIF_VMM_V0_PFNCLR                                                 0x06
#define NVIF_VMM_V0_RAW                                                    0x07
#define NVIF_VMM_V0_MTHD(i)                                         ((i) + 0x80)

struct nvif_vmm_raw_v0 {
	__u8 version;
#define NVIF_VMM_RAW_V0_GET	0x0
#define NVIF_VMM_RAW_V0_PUT	0x1
#define NVIF_VMM_RAW_V0_MAP	0x2
#define NVIF_VMM_RAW_V0_UNMAP	0x3
#define NVIF_VMM_RAW_V0_SPARSE	0x4
	__u8  op;
	__u8  sparse;
	__u8  ref;
	__u8  shift;
	__u32 argc;
	__u8  pad01[7];
	__u64 addr;
	__u64 size;
	__u64 offset;
	__u64 memory;
	__u64 argv;
};

struct nvif_vmm_pfnmap_v0 {
	__u8  version;
	__u8  page;
	__u8  pad02[6];
	__u64 addr;
	__u64 size;
#define NVIF_VMM_PFNMAP_V0_ADDR                           0xfffffffffffff000ULL
#define NVIF_VMM_PFNMAP_V0_ADDR_SHIFT                                        12
#define NVIF_VMM_PFNMAP_V0_APER                           0x00000000000000f0ULL
#define NVIF_VMM_PFNMAP_V0_HOST                           0x0000000000000000ULL
#define NVIF_VMM_PFNMAP_V0_VRAM                           0x0000000000000010ULL
#define NVIF_VMM_PFNMAP_V0_A				  0x0000000000000004ULL
#define NVIF_VMM_PFNMAP_V0_W                              0x0000000000000002ULL
#define NVIF_VMM_PFNMAP_V0_V                              0x0000000000000001ULL
#define NVIF_VMM_PFNMAP_V0_NONE                           0x0000000000000000ULL
	__u64 phys[];
};

struct nvif_vmm_pfnclr_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 addr;
	__u64 size;
};
#endif
