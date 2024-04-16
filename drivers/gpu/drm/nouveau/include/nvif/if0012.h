/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0012_H__
#define __NVIF_IF0012_H__

#include <drm/display/drm_dp.h>

#define NVIF_OUTP_V0_DP_DRIVE      0x74
#define NVIF_OUTP_V0_DP_SST        0x75
#define NVIF_OUTP_V0_DP_MST_ID_GET 0x76
#define NVIF_OUTP_V0_DP_MST_ID_PUT 0x77
#define NVIF_OUTP_V0_DP_MST_VCPI   0x78

union nvif_outp_dp_drive_args {
	struct nvif_outp_dp_drive_v0 {
		__u8  version;
		__u8  pad01[2];
		__u8  lanes;
		__u8  pe[4];
		__u8  vs[4];
	} v0;
};

union nvif_outp_dp_sst_args {
	struct nvif_outp_dp_sst_v0 {
		__u8  version;
		__u8  head;
		__u8  pad02[2];
		__u32 watermark;
		__u32 hblanksym;
		__u32 vblanksym;
	} v0;
};

union nvif_outp_dp_mst_id_put_args {
	struct nvif_outp_dp_mst_id_put_v0 {
		__u8  version;
		__u8  pad01[3];
		__u32 id;
	} v0;
};

union nvif_outp_dp_mst_id_get_args {
	struct nvif_outp_dp_mst_id_get_v0 {
		__u8  version;
		__u8  pad01[3];
		__u32 id;
	} v0;
};

union nvif_outp_dp_mst_vcpi_args {
	struct nvif_outp_dp_mst_vcpi_v0 {
		__u8  version;
		__u8  head;
		__u8  start_slot;
		__u8  num_slots;
		__u16 pbn;
		__u16 aligned_pbn;
	} v0;
};
#endif
