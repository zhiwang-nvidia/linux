/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0011_H__
#define __NVIF_IF0011_H__

union nvif_conn_event_args {
	struct nvif_conn_event_v0 {
		__u8 version;
#define NVIF_CONN_EVENT_V0_PLUG   0x01
#define NVIF_CONN_EVENT_V0_UNPLUG 0x02
#define NVIF_CONN_EVENT_V0_IRQ    0x04
		__u8 types;
		__u8 pad02[6];
	} v0;
};
#endif
