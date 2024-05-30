/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_PCI_H__
#define __NVKM_DEVICE_PCI_H__
#include <core/device.h>

struct nvkm_device_pci {
	struct nvkm_device device;
	struct pci_dev *pdev;

	struct dev_pm_domain vga_pm_domain;
};

extern struct pci_driver nvkm_device_pci_driver;
#endif
