/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VFIO_CXL_CORE_PRIV_H
#define VFIO_CXL_CORE_PRIV_H

#include <linux/vfio_pci_core.h>

#include "vfio_pci_priv.h"

void vfio_cxl_core_clean_register_emulation(struct vfio_cxl_core_device *cxl);
int vfio_cxl_core_setup_register_emulation(struct vfio_cxl_core_device *cxl);

ssize_t vfio_cxl_core_config_rw(struct vfio_device *vdev, char __user *buf,
				size_t count, loff_t *ppos, bool write);
ssize_t vfio_cxl_core_mmio_bar_rw(struct vfio_device *vdev, char __user *buf,
				  size_t count, loff_t *ppos, bool write);

#endif
