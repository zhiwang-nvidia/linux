// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/crc32.h>
#include <linux/firmware.h>

#include "debug.h"
#include "vgpu_mgr.h"
#include "metadata.h"

#include <nvrm/gsp.h>

/* Sanity checks on main headers */
static int check_main_headers(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	struct metadata_blob_hdr *blob;
	u32 crc;

	if (fw->size <= sizeof(*hdr)) {
		vgpu_mgr_error(vgpu_mgr, "metadata: file is too small\n");
		return -EINVAL;
	}

	crc = crc32_le(0xffffffff, fw->data + 16, fw->size - 16);
	if (crc != hdr->crc32) {
		vgpu_mgr_error(vgpu_mgr, "metadata: invalid CRC\n");
		return -EINVAL;
	}

	if (memcmp(&hdr->identifier, METADATA_IDR, sizeof(hdr->identifier))) {
		vgpu_mgr_error(vgpu_mgr, "metadata: invalid identifier\n");
		return -EINVAL;
	}

	if (!hdr->num_blobs ||
	    (hdr->num_blobs > (fw->size - sizeof(*hdr)) / sizeof(*blob))) {
		vgpu_mgr_error(vgpu_mgr, "metadata: invalid num_blobs\n");
		return -EINVAL;
	}
	return 0;
}

static int get_running_gsp_build_version(struct nvidia_vgpu_mgr *vgpu_mgr,
					 char *running_gsp_build_version)
{
	NV2080_CTRL_GSP_GET_FEATURES_PARAMS *ctrl;

	ctrl = nvidia_vgpu_mgr_rm_ctrl_rd(vgpu_mgr, &vgpu_mgr->gsp_client,
			NV2080_CTRL_CMD_GSP_GET_FEATURES, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	memcpy(running_gsp_build_version, ctrl->firmwareVersion, GSP_MAX_BUILD_VERSION_LENGTH);

	nvidia_vgpu_mgr_rm_ctrl_done(vgpu_mgr, &vgpu_mgr->gsp_client, ctrl);

	vgpu_mgr_debug(vgpu_mgr, "running GSP build version %s\n", running_gsp_build_version);

	return 0;
}

struct version {
	u64 vgpu_major;
	u64 vgpu_minor;
	const char *gsp_build_version;
};

static struct version supported_version_list[] = {
	{ 18, 1, "570.144" },
};

/* check supported versions */
static int check_versions(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw,
			  char *running_gsp_build_version)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	unsigned int i;

	/*
	 * The running GSP metadata supports vGPU (or we won't be here).
	 * Check if the vGPU metadata file matches with the version of GSP metadata.
	 */
	if (strncmp(running_gsp_build_version, hdr->gsp_build_version,
		    GSP_MAX_BUILD_VERSION_LENGTH)) {
		vgpu_mgr_error(vgpu_mgr, "unexpected metadata GSP version %s, running %s\n",
			       hdr->gsp_build_version, running_gsp_build_version);
		return -EINVAL;
	}

	/* Check vGPU release version. */
	for (i = 0; i < ARRAY_SIZE(supported_version_list); i++) {
		struct version *v = supported_version_list + i;

		if (strncmp(v->gsp_build_version, hdr->gsp_build_version,
			    GSP_MAX_BUILD_VERSION_LENGTH))
			continue;

		if (v->vgpu_major == hdr->vgpu_major && v->vgpu_minor == hdr->vgpu_minor)
			break;
	}

	if (i == ARRAY_SIZE(supported_version_list)) {
		vgpu_mgr_error(vgpu_mgr, "unexpected metadata vGPU %llu.%llu GSP %s, running %s\n",
			       hdr->vgpu_major, hdr->vgpu_minor, hdr->gsp_build_version,
			       running_gsp_build_version);
		return -EINVAL;
	}

	return 0;
}

#define for_each_blob(hdr, blob, i) \
	for (i = 0, blob = (typeof(blob))hdr->data; i < hdr->num_blobs; \
	     i++, blob = ((void *)blob) + blob->size)

/* Sanity check on blob headers */
static int check_blob_headers(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	struct metadata_blob_hdr *blob;
	unsigned int i;

	for_each_blob(hdr, blob, i) {
		vgpu_mgr_debug(vgpu_mgr, "check blob header %u type 0x%llx size 0x%llx\n",
			       i, blob->type, blob->size);

		if (blob->type >= METADATA_BLOB_MAX) {
			vgpu_mgr_error(vgpu_mgr, "unknown blob type 0x%llx\n", blob->type);
			return -EINVAL;
		}

		if (blob->size <= sizeof(*blob) ||
		    (blob->size > (fw->size - ((void *)blob - (void *)fw->data)))) {
			vgpu_mgr_error(vgpu_mgr, "invalid blob_size 0x%llx\n", blob->size);
			return -EINVAL;
		}
	}
	return 0;
}

typedef int (*blob_handler_t)(struct nvidia_vgpu_mgr *vgpu_mgr, void *blob, u64 blob_size);

struct blob_handler_fn {
	blob_handler_t check;
	blob_handler_t setup;
	blob_handler_t post_setup;
	blob_handler_t clean;
};

struct blob_handler_fn blob_handlers[] = {
	[METADATA_BLOB_VGPU_TYPE] = {
		.check = nvidia_vgpu_metadata_check_vgpu_type,
		.setup = nvidia_vgpu_metadata_setup_vgpu_type,
		.post_setup = nvidia_vgpu_metadata_post_setup_vgpu_type,
		.clean = nvidia_vgpu_metadata_clean_vgpu_type,
	},
};

/* Check blobs in this metadata file */
static int check_blobs(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	struct metadata_blob_hdr *blob;
	unsigned int i;
	int ret;

	for_each_blob(hdr, blob, i) {
		ret = blob_handlers[blob->type].check(vgpu_mgr, blob->data, blob->size);
		if (ret) {
			vgpu_mgr_error(vgpu_mgr, "metadata: blob is invalid, type: 0x%llx\n",
				       blob->type);
			return ret;
		}
	}
	return 0;
}

/* Setup blobs in this metadata file */
static int setup_blobs(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	struct metadata_blob_hdr *blob;
	unsigned int i;
	int ret;

	for_each_blob(hdr, blob, i) {
		ret = blob_handlers[blob->type].setup(vgpu_mgr, blob->data, blob->size);
		if (ret) {
			vgpu_mgr_error(vgpu_mgr, "metadata: fail to setup blob, type: 0x%llx\n",
				       blob->type);
			return ret;
		}
	}
	return 0;
}

/* Final setup after installing all the blobs */
static int post_setup_blobs(struct nvidia_vgpu_mgr *vgpu_mgr, const struct firmware *fw)
{
	struct metadata_hdr *hdr = (struct metadata_hdr *)fw->data;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(blob_handlers); i++) {
		ret = blob_handlers[i].post_setup(vgpu_mgr, NULL, 0);
		if (ret) {
			vgpu_mgr_error(vgpu_mgr, "metadata: fail to post setup blob, type: 0x%x\n",
				       i);
			return ret;
		}
	}

	vgpu_mgr->vgpu_major = hdr->vgpu_major;
	vgpu_mgr->vgpu_minor = hdr->vgpu_minor;

	return 0;
}

/* Clean all the installed blobs */
static void clean_blobs(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(blob_handlers); i++)
		blob_handlers[i].clean(vgpu_mgr, NULL, 0);
}

/**
 * nvidia_vgpu_mgr_clean_metadata - clean vGPU metadata
 * @vgpu_mgr: the vGPU manager.
 */
void nvidia_vgpu_mgr_clean_metadata(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	clean_blobs(vgpu_mgr);

	vgpu_mgr_debug(vgpu_mgr, "clean vgpu metadata\n");
}

/**
 * nvidia_vgpu_mgr_setup_metadata - setup vGPU metadata
 * @vgpu_mgr: the vGPU manager.
 *
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_mgr_setup_metadata(struct nvidia_vgpu_mgr *vgpu_mgr)
{
	u8 running_gsp_build_version[GSP_MAX_BUILD_VERSION_LENGTH];
	char *path;
	const struct firmware *fw;
	int ret = 0;

	ret = get_running_gsp_build_version(vgpu_mgr, running_gsp_build_version);
	if (ret)
		return ret;

	path = kvzalloc(PATH_MAX, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	snprintf(path, PATH_MAX, METADATA_PATH "vgpu-%s.bin",
		 running_gsp_build_version);

	vgpu_mgr_debug(vgpu_mgr, "request vgpu metadata %s\n", path);

	ret = request_firmware(&fw, path, &vgpu_mgr->handle.pf_pdev->dev);

	kvfree(path);

	if (ret)
		return ret;

	vgpu_mgr_debug(vgpu_mgr, "check main headers\n");

	ret = check_main_headers(vgpu_mgr, fw);
	if (ret)
		goto out_free_fw;

	vgpu_mgr_debug(vgpu_mgr, "check versions\n");

	ret = check_versions(vgpu_mgr, fw, running_gsp_build_version);
	if (ret)
		goto out_free_fw;

	vgpu_mgr_debug(vgpu_mgr, "check blob headers\n");

	ret = check_blob_headers(vgpu_mgr, fw);
	if (ret)
		goto out_free_fw;

	vgpu_mgr_debug(vgpu_mgr, "check blobs\n");

	ret = check_blobs(vgpu_mgr, fw);
	if (ret)
		goto out_free_fw;

	vgpu_mgr_debug(vgpu_mgr, "setup blobs\n");

	ret = setup_blobs(vgpu_mgr, fw);
	if (ret)
		goto out_free_fw;

	vgpu_mgr_debug(vgpu_mgr, "post-setup blobs\n");

	ret = post_setup_blobs(vgpu_mgr, fw);
	if (ret) {
		clean_blobs(vgpu_mgr);
		goto out_free_fw;
	}

	vgpu_mgr_debug(vgpu_mgr, "metadata loaded, vgpu major %llu vgpu minor %llu\n",
		       vgpu_mgr->vgpu_major, vgpu_mgr->vgpu_minor);

out_free_fw:
	release_firmware(fw);
	return ret;
}
