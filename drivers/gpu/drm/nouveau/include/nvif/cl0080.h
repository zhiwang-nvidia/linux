/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CL0080_H__
#define __NVIF_CL0080_H__

#define NV_DEVICE_V0_INFO                                                  0x00

struct nv_device_info_v0 {
	__u8  version;
#define NV_DEVICE_INFO_V0_IGP                                              0x00
#define NV_DEVICE_INFO_V0_PCI                                              0x01
#define NV_DEVICE_INFO_V0_AGP                                              0x02
#define NV_DEVICE_INFO_V0_PCIE                                             0x03
#define NV_DEVICE_INFO_V0_SOC                                              0x04
	__u8  platform;
	__u16 chipset;	/* from NV_PMC_BOOT_0 */
	__u8  revision;	/* from NV_PMC_BOOT_0 */
#define NV_DEVICE_INFO_V0_TNT                                              0x01
#define NV_DEVICE_INFO_V0_CELSIUS                                          0x02
#define NV_DEVICE_INFO_V0_KELVIN                                           0x03
#define NV_DEVICE_INFO_V0_RANKINE                                          0x04
#define NV_DEVICE_INFO_V0_CURIE                                            0x05
#define NV_DEVICE_INFO_V0_TESLA                                            0x06
#define NV_DEVICE_INFO_V0_FERMI                                            0x07
#define NV_DEVICE_INFO_V0_KEPLER                                           0x08
#define NV_DEVICE_INFO_V0_MAXWELL                                          0x09
#define NV_DEVICE_INFO_V0_PASCAL                                           0x0a
#define NV_DEVICE_INFO_V0_VOLTA                                            0x0b
#define NV_DEVICE_INFO_V0_TURING                                           0x0c
#define NV_DEVICE_INFO_V0_AMPERE                                           0x0d
#define NV_DEVICE_INFO_V0_ADA                                              0x0e
	__u8  family;
	__u8  pad06[2];
	__u64 ram_size;
	__u64 ram_user;
	char  chip[16];
	char  name[64];
};
#endif
