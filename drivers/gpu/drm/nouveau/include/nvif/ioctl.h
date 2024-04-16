/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IOCTL_H__
#define __NVIF_IOCTL_H__

struct nvif_ioctl_v0 {
	__u8  version;
#define NVIF_IOCTL_V0_SCLASS                                               0x01
#define NVIF_IOCTL_V0_NEW                                                  0x02
#define NVIF_IOCTL_V0_DEL                                                  0x03
#define NVIF_IOCTL_V0_MTHD                                                 0x04
	__u8  type;
	__u8  pad02[4];
#define NVIF_IOCTL_V0_OWNER_NVIF                                           0x00
#define NVIF_IOCTL_V0_OWNER_ANY                                            0xff
	__u8  owner;
#define NVIF_IOCTL_V0_ROUTE_NVIF                                           0x00
#define NVIF_IOCTL_V0_ROUTE_HIDDEN                                         0xff
	__u8  route;
	__u64 token;
	__u64 object;
	__u8  data[];		/* ioctl data (below) */
};

struct nvif_ioctl_sclass_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	struct nvif_ioctl_sclass_oclass_v0 {
		__s32 oclass;
		__s16 minver;
		__s16 maxver;
	} oclass[];
};

struct nvif_ioctl_new_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  pad01[6];
	__u8  route;
	__u64 token;
	__u64 object;
	__u32 handle;
	__s32 oclass;
	__u8  data[];		/* class data (class.h) */
};

struct nvif_ioctl_del {
};

struct nvif_ioctl_mthd_v0 {
	/* nvif_ioctl ... */
	__u8  version;
	__u8  method;
	__u8  pad02[6];
	__u8  data[];		/* method data (class.h) */
};
#endif
