/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0001_H__
#define __NVIF_IF0001_H__

#define NVIF_CONTROL_PSTATE_ATTR                                           0x01
#define NVIF_CONTROL_PSTATE_USER                                           0x02

struct nvif_control_pstate_attr_v0 {
	__u8  version;
#define NVIF_CONTROL_PSTATE_ATTR_V0_STATE_CURRENT                          (-1)
	__s8  state; /*  in: index of pstate to query
		      * out: pstate identifier
		      */
	__u8  index; /*  in: index of attribute to query
		      * out: index of next attribute, or 0 if no more
		      */
	__u8  pad03[5];
	__u32 min;
	__u32 max;
	char  name[32];
	char  unit[16];
};

struct nvif_control_pstate_user_v0 {
	__u8  version;
#define NVIF_CONTROL_PSTATE_USER_V0_STATE_UNKNOWN                          (-1)
#define NVIF_CONTROL_PSTATE_USER_V0_STATE_PERFMON                          (-2)
	__s8  ustate; /*  in: pstate identifier */
	__s8  pwrsrc; /*  in: target power source */
	__u8  pad03[5];
};
#endif
