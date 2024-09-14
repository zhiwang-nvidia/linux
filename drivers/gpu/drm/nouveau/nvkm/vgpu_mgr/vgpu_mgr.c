/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */
#include <core/device.h>
#include <core/driver.h>
#include <nvif/driverif.h>
#include <core/pci.h>

#include <subdev/gsp.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/nvidia/inc/kernel/gpu/gsp/gsp_static_config.h>

#include <vgpu_mgr/vgpu_mgr.h>

static bool support_vgpu_mgr = false;
module_param_named(support_vgpu_mgr, support_vgpu_mgr, bool, 0400);

static inline struct pci_dev *nvkm_to_pdev(struct nvkm_device *device)
{
	struct nvkm_device_pci *pci = container_of(device, typeof(*pci),
						   device);

	return pci->pdev;
}

/**
 * nvkm_vgpu_mgr_is_supported - check if a platform support vGPU
 * @device: the nvkm_device pointer
 *
 * Returns: true on supported platform which is newer than ADA Lovelace
 * with SRIOV support.
 */
bool nvkm_vgpu_mgr_is_supported(struct nvkm_device *device)
{
	struct pci_dev *pdev = nvkm_to_pdev(device);

	if (!support_vgpu_mgr)
		return false;

	return device->card_type == AD100 &&  pci_sriov_get_totalvfs(pdev);
}

/**
 * nvkm_vgpu_mgr_is_enabled - check if vGPU support is enabled on a PF
 * @device: the nvkm_device pointer
 *
 * Returns: true if vGPU enabled.
 */
bool nvkm_vgpu_mgr_is_enabled(struct nvkm_device *device)
{
	return device->vgpu_mgr.enabled;
}

static void detach_nvkm(struct nvkm_vgpu_mgr *vgpu_mgr)
{
	if (vgpu_mgr->dev_impl) {
		vgpu_mgr->dev_impl->del(vgpu_mgr->dev_priv);
		vgpu_mgr->dev_impl = NULL;
	}

	if (vgpu_mgr->cli_impl) {
		vgpu_mgr->cli_impl->del(vgpu_mgr->cli_priv);
		vgpu_mgr->cli_impl = NULL;
	}
}

static int attach_nvkm(struct nvkm_vgpu_mgr *vgpu_mgr)
{
	struct nvkm_device *device = vgpu_mgr->nvkm_dev;
	int ret;

	ret = nvkm_driver_ctor(device, &vgpu_mgr->driver,
			       &vgpu_mgr->cli_impl, &vgpu_mgr->cli_priv);
	if (ret)
		return ret;

	ret = vgpu_mgr->cli_impl->device.new(vgpu_mgr->cli_priv,
					     &vgpu_mgr->dev_impl,
					     &vgpu_mgr->dev_priv);
	if (ret)
		goto fail_device_new;

	return 0;

fail_device_new:
	vgpu_mgr->cli_impl->del(vgpu_mgr->cli_priv);
	vgpu_mgr->cli_impl = NULL;

	return ret;
}

static int get_vmmu_segment_size(struct nvkm_vgpu_mgr *mgr)
{
	struct nvkm_device *device = mgr->nvkm_dev;
	struct nvkm_gsp *gsp = device->gsp;
	NV2080_CTRL_GPU_GET_VMMU_SEGMENT_SIZE_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_GPU_GET_VMMU_SEGMENT_SIZE,
				    sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	nvdev_debug(device, "VMMU segment size: %llx\n", ctrl->vmmuSegmentSize);

	mgr->vmmu_segment_size = ctrl->vmmuSegmentSize;

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return 0;
}

/**
 * nvkm_vgpu_mgr_init - Initialize the vGPU manager support
 * @device: the nvkm_device pointer
 *
 * Returns: 0 on success, -ENODEV on platforms that are not supported.
 */
int nvkm_vgpu_mgr_init(struct nvkm_device *device)
{
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;
	int ret;

	if (!nvkm_vgpu_mgr_is_supported(device))
		return -ENODEV;

	vgpu_mgr->nvkm_dev = device;

	ret = attach_nvkm(vgpu_mgr);
	if (ret)
		return ret;

	ret = get_vmmu_segment_size(vgpu_mgr);
	if (ret)
		goto err_get_vmmu_seg_size;

	nvkm_vgpu_mgr_init_vfio_ops(vgpu_mgr);

	vgpu_mgr->enabled = true;
	pci_info(nvkm_to_pdev(device),
		 "NVIDIA vGPU mananger support is enabled.\n");

	return 0;

err_get_vmmu_seg_size:
	detach_nvkm(vgpu_mgr);
	return ret;
}

/**
 * nvkm_vgpu_mgr_fini - De-initialize the vGPU manager support
 * @device: the nvkm_device pointer
 */
void nvkm_vgpu_mgr_fini(struct nvkm_device *device)
{
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	detach_nvkm(vgpu_mgr);
	vgpu_mgr->enabled = false;
}

/**
 * nvkm_vgpu_mgr_populate_vf_info - populate GSP_VF_INFO when vGPU
 * is enabled
 * @device: the nvkm_device pointer
 * @info: GSP_VF_INFO data structure
 */
void nvkm_vgpu_mgr_populate_gsp_vf_info(struct nvkm_device *device,
					void *info)
{
	struct pci_dev *pdev = nvkm_to_pdev(device);
	GspSystemInfo *gsp_info = info;
	GSP_VF_INFO *vf_info = &gsp_info->gspVFInfo;
	u32 lo, hi;
	u16 v;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);

	pci_read_config_word(pdev, pos + PCI_SRIOV_TOTAL_VF, &v);
	vf_info->totalVFs = v;

	pci_read_config_word(pdev, pos + PCI_SRIOV_VF_OFFSET, &v);
	vf_info->firstVFOffset = v;

	pci_read_config_dword(pdev, pos + PCI_SRIOV_BAR, &lo);
	vf_info->FirstVFBar0Address = lo & 0xFFFFFFF0;

	pci_read_config_dword(pdev, pos + PCI_SRIOV_BAR + 4, &lo);
	pci_read_config_dword(pdev, pos + PCI_SRIOV_BAR + 8, &hi);

	vf_info->FirstVFBar1Address = (((u64)hi) << 32) + (lo & 0xFFFFFFF0);

	pci_read_config_dword(pdev, pos + PCI_SRIOV_BAR + 12, &lo);
	pci_read_config_dword(pdev, pos + PCI_SRIOV_BAR + 16, &hi);

	vf_info->FirstVFBar2Address = (((u64)hi) << 32) + (lo & 0xFFFFFFF0);

#define IS_BAR_64(i) (((i) & 0x00000006) == 0x00000004)

	v = nvkm_rd32(device, 0x88000 + 0xbf4);
	vf_info->b64bitBar1 = IS_BAR_64(v);

	v = nvkm_rd32(device, 0x88000 + 0xbfc);
	vf_info->b64bitBar2 = IS_BAR_64(v);
}
