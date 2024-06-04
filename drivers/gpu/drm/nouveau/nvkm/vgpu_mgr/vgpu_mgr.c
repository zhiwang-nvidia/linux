/* SPDX-License-Identifier: MIT */
#include <core/device.h>
#include <core/pci.h>
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

/**
 * nvkm_vgpu_mgr_init - Initialize the vGPU manager support
 * @device: the nvkm_device pointer
 *
 * Returns: 0 on success, -ENODEV on platforms that are not supported.
 */
int nvkm_vgpu_mgr_init(struct nvkm_device *device)
{
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	if (!nvkm_vgpu_mgr_is_supported(device))
		return -ENODEV;

	vgpu_mgr->nvkm_dev = device;
	vgpu_mgr->enabled = true;

	pci_info(nvkm_to_pdev(device),
		 "NVIDIA vGPU mananger support is enabled.\n");

	return 0;
}

/**
 * nvkm_vgpu_mgr_fini - De-initialize the vGPU manager support
 * @device: the nvkm_device pointer
 */
void nvkm_vgpu_mgr_fini(struct nvkm_device *device)
{
	struct nvkm_vgpu_mgr *vgpu_mgr = &device->vgpu_mgr;

	vgpu_mgr->enabled = false;
}
