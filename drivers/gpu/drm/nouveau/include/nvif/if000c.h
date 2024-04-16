#ifndef __NVIF_IF000C_H__
#define __NVIF_IF000C_H__

#define NVIF_VMM_V0_RAW                                                    0x07
#define NVIF_VMM_V0_MTHD(i)                                         ((i) + 0x80)

struct nvif_vmm_raw_v0 {
	__u8 version;
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
#endif
