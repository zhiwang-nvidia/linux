// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include "debug.h"
#include "vgpu_mgr.h"
#include "metadata.h"

#include <nvrm/gsp.h>

/**
 * nvidia_vgpu_metadata_check_vgpu_type - check vGPU type blobs
 * @vgpu_mgr: the vGPU manager
 * @blob: the blob header
 * @blob_size: the blob size
 *
 * Returns: zero on success, others on errors.
 */
int nvidia_vgpu_metadata_check_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr,
					 void *blob, u64 blob_size)
{
	struct vgpu_type_blob_hdr *hdr = blob;
	u64 size;

	vgpu_mgr_debug(vgpu_mgr, "check vgpu type blob for device 0x%llx\n", hdr->device_id);

	if (!hdr->device_id || !hdr->num_kernel_structs || !hdr->kernel_struct_size ||
	    !hdr->gsp_rmctrl_cmd || !hdr->gsp_rmctrl_size) {
		vgpu_mgr_error(vgpu_mgr, "metadata: vgpu type blob header is invalid\n");
		return -EINVAL;
	}

	size = sizeof(struct metadata_blob_hdr);
	size += sizeof(*hdr);
	size += hdr->kernel_struct_size;
	size += hdr->gsp_rmctrl_size;

	if (size != blob_size) {
		vgpu_mgr_error(vgpu_mgr, "metadata: vgpu type blob size mismatch\n");
		return -EINVAL;
	}
	return 0;
}

static int send_gsp_rmctrl(struct nvidia_vgpu_mgr *vgpu_mgr, struct vgpu_type_blob_hdr *hdr)
{
	void *ctrl;
	int ret;

	vgpu_mgr_debug(vgpu_mgr, "send rmctrl cmd 0x%llx size 0x%llx\n", hdr->gsp_rmctrl_cmd,
		       hdr->gsp_rmctrl_size);

	ctrl = nvidia_vgpu_mgr_rm_ctrl_get(vgpu_mgr, &vgpu_mgr->gsp_client,
					   hdr->gsp_rmctrl_cmd, hdr->gsp_rmctrl_size);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	memcpy(ctrl, hdr->data + hdr->kernel_struct_size, hdr->gsp_rmctrl_size);

	ret = nvidia_vgpu_mgr_rm_ctrl_wr(vgpu_mgr, &vgpu_mgr->gsp_client, ctrl);
	if (ret)
		return ret;

	return 0;
}

/**
 * nvidia_vgpu_metadata_setup_vgpu_type - setup vGPU type blob
 * @vgpu_mgr: the vGPU manager
 * @blob: the blob header
 * @blob_size: the blob size
 *
 * Returns: zero on success, others on errors.
 */
int nvidia_vgpu_metadata_setup_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					 u64 blob_size)
{
	struct vgpu_type_blob_hdr *hdr = blob;
	u64 size, copy_size;
	int ret;
	void *p;
	int i;

	/* Not for this device, skip */
	if (hdr->device_id != vgpu_mgr->handle.pf_pdev->device)
		return 0;

	vgpu_mgr_debug(vgpu_mgr, "setup vgpu type blob for device 0x%llx\n", hdr->device_id);

	vgpu_mgr->vgpu_types = kvrealloc(vgpu_mgr->vgpu_types, hdr->kernel_struct_size, GFP_KERNEL);
	if (!vgpu_mgr->vgpu_types)
		return -ENOMEM;

	ret = send_gsp_rmctrl(vgpu_mgr, hdr);
	if (ret) {
		kvfree(vgpu_mgr->vgpu_types);
		vgpu_mgr->vgpu_types = NULL;
		return ret;
	}

	size = hdr->kernel_struct_size / hdr->num_kernel_structs;
	copy_size = min(size, sizeof(struct nvidia_vgpu_type));
	p = hdr->data;

	for (i = 0; i < hdr->num_kernel_structs; i++, p += size) {
		memcpy(vgpu_mgr->vgpu_types + i, p, copy_size);

		vgpu_mgr_debug(vgpu_mgr, "setup vgpu type %u %s for device 0x%llx\n",
			       vgpu_mgr->vgpu_types[i].vgpu_type,
			       vgpu_mgr->vgpu_types[i].vgpu_type_name, hdr->device_id);
	}

	vgpu_mgr->num_vgpu_types = hdr->num_kernel_structs;
	return 0;
}

/**
 * nvidia_vgpu_metadata_post_setup_vgpu_type - vGPU type post setup
 *
 * @vgpu_mgr: the vGPU manager
 * @blob: the blob header
 * @blob_size: the blob size
 *
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_metadata_post_setup_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					      u64 blob_size)
{
	if (WARN_ON(!vgpu_mgr->vgpu_types || !vgpu_mgr->num_vgpu_types)) {
		vgpu_mgr_error(vgpu_mgr, "metadata: no available vgpu type blob\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * nvidia_vgpu_metadata_clean_vgpu_type - clean vGPU type
 *
 * @vgpu_mgr: the vGPU manager
 * @blob: the blob header
 * @blob_size: the blob size
 *
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_metadata_clean_vgpu_type(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob,
					 u64 blob_size)
{
	kvfree(vgpu_mgr->vgpu_types);
	vgpu_mgr->vgpu_types = NULL;
	vgpu_mgr->num_vgpu_types = 0;
	return 0;
}
