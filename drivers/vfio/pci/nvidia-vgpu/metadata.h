/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025 NVIDIA Corporation
 */
#ifndef __NVIDIA_VGPU_METADATA_H__
#define __NVIDIA_VGPU_METADATA_H__

#define METADATA_PATH "nvidia/"
#define METADATA_IDR "NVVGPUMT"

enum {
	METADATA_BLOB_VGPU_TYPE = 0,
	METADATA_BLOB_MAX,
};

#define GSP_MAX_BUILD_VERSION_LENGTH (0x0000040)

#define METADATA_VGPU_FEATURE_SIZE 128

/**
 * struct metadata_hdr - vGPU metafile main header
 *
 * @identifier: identifier to check
 * @crc32: crc32 of the metafile
 * @vgpu_major: vGPU major version
 * @vgpu_minor: vGPU minor version
 * @vgpu_features: vGPU features in a specific version
 * @gsp_build_version: GSP build version for this vGPU version
 * @num_blobs: total blob amount
 * @data: blob data
 */
struct metadata_hdr {
	u64 identifier; /* "NVVGPUMT" */
	u32 crc32;
	u32 padding;
	u64 vgpu_major;
	u64 vgpu_minor;
	u8 vgpu_features[METADATA_VGPU_FEATURE_SIZE];
	u8 gsp_build_version[GSP_MAX_BUILD_VERSION_LENGTH];
	u64 num_blobs;
	unsigned char data[];
};

/**
 * struct metadata_blob_hdr - vGPU metafile blob section header
 *
 * @type: blob type
 * @size: blob size
 * @data: blob data
 */
struct metadata_blob_hdr {
	u64 type;
	u64 size;
	unsigned char data[];
};

/**
 * struct vgpu_type_blob_hdr - vGPU metafile vGPU type blob header
 *
 * @device_id: supported device ID
 * @gsp_rmctrl_vgpu_info_offset: vgpu info offset in rmctrl part
 * @gsp_rmctrl_vgpu_info_szie: vgpu info size in rmctrl part
 * @kernel_struct_size: kernel struct size
 * @num_kernel_struct: amount of kernel structs
 * @gsp_rmctrl_cmd: GSP rmctrl command
 * @gsp_rmctrl_size: GSP rmctl size
 * @data: blob data
 */
struct vgpu_type_blob_hdr {
	u64 device_id;
	u64 gsp_rmctrl_vgpu_info_offset;
	u64 gsp_rmctrl_vgpu_info_size;

	u64 kernel_struct_size;
	u64 num_kernel_structs;
	u64 gsp_rmctrl_cmd;
	u64 gsp_rmctrl_size;
	unsigned char data[];
};

int nvidia_vgpu_metadata_check_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr,
					 void *blob, u64 blob_size);
int nvidia_vgpu_metadata_setup_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					 u64 blob_size);
int nvidia_vgpu_metadata_post_setup_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					      u64 blob_size);
int nvidia_vgpu_metadata_clean_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					 u64 blob_size);
#endif
