/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright © 2024 NVIDIA Corporation
 */

#include <linux/string.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "vfio.h"

void nvidia_vgpu_vfio_setup_config(struct nvidia_vgpu_vfio *nvdev)
{
	u8 *buffer = NULL;

	memset(nvdev->config_space, 0, sizeof(nvdev->config_space));

	/* Header type 0 (normal devices) */
	*(u16 *)&nvdev->config_space[PCI_VENDOR_ID] = 0x10de;
	*(u16 *)&nvdev->config_space[PCI_DEVICE_ID] =
		FIELD_GET(GENMASK(31, 16), nvdev->curr_vgpu_type->vdevId);
	*(u16 *)&nvdev->config_space[PCI_COMMAND] = 0x0000;
	*(u16 *)&nvdev->config_space[PCI_STATUS] = 0x0010;

	buffer = &nvdev->config_space[PCI_CLASS_REVISION];
	pci_read_config_byte(nvdev->core_dev.pdev, PCI_CLASS_REVISION, buffer);

	nvdev->config_space[PCI_CLASS_PROG] = 0; /* VGA-compatible */
	nvdev->config_space[PCI_CLASS_DEVICE] = 0; /* VGA controller */
	nvdev->config_space[PCI_CLASS_DEVICE + 1] = 3; /* display controller */

	/* BAR0: 32-bit */
	*(u32 *)&nvdev->config_space[PCI_BASE_ADDRESS_0] = 0x00000000;
	/* BAR1: 64-bit, prefetchable */
	*(u32 *)&nvdev->config_space[PCI_BASE_ADDRESS_1] = 0x0000000c;
	/* BAR2: 64-bit, prefetchable */
	*(u32 *)&nvdev->config_space[PCI_BASE_ADDRESS_3] = 0x0000000c;
	/* Disable BAR3: I/O */
	*(u32 *)&nvdev->config_space[PCI_BASE_ADDRESS_5] = 0x00000000;

	*(u16 *)&nvdev->config_space[PCI_SUBSYSTEM_VENDOR_ID] = 0x10de;
	*(u16 *)&nvdev->config_space[PCI_SUBSYSTEM_ID] =
		FIELD_GET(GENMASK(15, 0), nvdev->curr_vgpu_type->vdevId);

	nvdev->config_space[PCI_CAPABILITY_LIST] = CAP_LIST_NEXT_PTR_MSIX;
	nvdev->config_space[CAP_LIST_NEXT_PTR_MSIX + 1] = 0x0;

	/* INTx disabled */
	nvdev->config_space[0x3d] = 0;
}

static void read_hw_pci_config(struct pci_dev *pdev, char *buf,
			       size_t count, loff_t offset)
{
	switch (count) {
	case 4:
		pci_read_config_dword(pdev, offset, (u32 *)buf);
		break;

	case 2:
		pci_read_config_word(pdev, offset, (u16 *)buf);
		break;

	case 1:
		pci_read_config_byte(pdev, offset, (u8 *)buf);
		break;
	default:
		WARN_ONCE(1, "Not supported access len\n");
		break;
	}
}

static void write_hw_pci_config(struct pci_dev *pdev, char *buf,
				size_t count, loff_t offset)
{
	switch (count) {
	case 4:
		pci_write_config_dword(pdev, offset, *(u32 *)buf);
		break;

	case 2:
		pci_write_config_word(pdev, offset, *(u16 *)buf);
		break;

	case 1:
		pci_write_config_byte(pdev, offset, *(u8 *)buf);
		break;
	default:
		WARN_ONCE(1, "Not supported access len\n");
		break;
	}
}

static void hw_pci_config_rw(struct pci_dev *pdev, char *buf,
			     size_t count, loff_t offset,
			     bool is_write)
{
	is_write ? write_hw_pci_config(pdev, buf, count, offset) :
		   read_hw_pci_config(pdev, buf, count, offset);
}

static ssize_t bar0_rw(struct nvidia_vgpu_vfio *nvdev, char *buf,
		       size_t count, loff_t ppos, bool iswrite)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	int index = VFIO_PCI_OFFSET_TO_INDEX(ppos);
	loff_t offset = ppos;
	void __iomem *map;
	u32 val;
	int ret;

	if (index != VFIO_PCI_BAR0_REGION_INDEX)
		return -EINVAL;

	offset &= VFIO_PCI_OFFSET_MASK;

	if (nvdev->bar0_map == NULL) {
		ret = pci_request_selected_regions(pdev, 1 << index, "nvidia-vgpu-vfio");
		if (ret)
			return ret;

		if (!(pci_resource_flags(pdev, index) & IORESOURCE_MEM)) {
			pci_release_selected_regions(pdev, 1 << index);
			return -EIO;
		}

		map = ioremap(pci_resource_start(pdev, index), pci_resource_len(pdev, index));
		if (!map) {
			pci_err(pdev, "Can't map BAR0 MMIO space\n");
			pci_release_selected_regions(pdev, 1 << index);
			return -ENOMEM;
		}
		nvdev->bar0_map = map;
	} else
		map = nvdev->bar0_map;

	if (!iswrite) {
		switch (count) {
		case 4:
			val = ioread32(map + offset);
			break;
		case 2:
			val = ioread16(map + offset);
			break;
		case 1:
			val = ioread8(map + offset);
			break;
		}
		memcpy(buf, (u8 *)&val, count);
	} else {
		switch (count) {
		case 4:
			iowrite32(*(u32 *)buf, map + offset);
			break;
		case 2:
			iowrite16(*(u16 *)buf, map + offset);
			break;
		case 1:
			iowrite8(*(u8 *)buf, map + offset);
			break;
		}
	}
	return count;
}

static ssize_t pci_config_rw(struct nvidia_vgpu_vfio *nvdev, char *buf,
			     size_t count, loff_t ppos, bool iswrite)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	int index = VFIO_PCI_OFFSET_TO_INDEX(ppos);
	loff_t offset = ppos;
	u32 bar_mask, cfg_addr;
	u32 val = 0;

	if (index != VFIO_PCI_CONFIG_REGION_INDEX)
		return -EINVAL;

	offset &= VFIO_PCI_OFFSET_MASK;

	if ((offset >= CAP_LIST_NEXT_PTR_MSIX) && (offset <
				(CAP_LIST_NEXT_PTR_MSIX + MSIX_CAP_SIZE))) {
		hw_pci_config_rw(pdev, buf, count, offset, iswrite);
		return count;
	}

	if (!iswrite) {
		memcpy(buf, (u8 *)&nvdev->config_space[offset], count);

		switch (offset) {
		case PCI_COMMAND:
			hw_pci_config_rw(pdev, (char *)&val, count, offset, iswrite);

			switch (count) {
			case 4:
				val = (u32)(val & 0xFFFF0000) | (val &
					(PCI_COMMAND_PARITY | PCI_COMMAND_SERR));
				break;
			case 2:
				val = (val & (PCI_COMMAND_PARITY | PCI_COMMAND_SERR));
				break;
			default:
				WARN_ONCE(1, "Not supported access len\n");
				break;
			}
			break;
		case PCI_STATUS:
			hw_pci_config_rw(pdev, (char *)&val, count, offset, iswrite);
			break;

		default:
			break;
		}
		*(u32 *)buf = *(u32 *)buf | val;
	} else {
		switch (offset) {
		case PCI_VENDOR_ID:
		case PCI_DEVICE_ID:
		case PCI_CAPABILITY_LIST:
			break;

		case PCI_STATUS:
			hw_pci_config_rw(pdev, buf, count, offset, iswrite);
			break;

		case PCI_COMMAND:
			if (count == 4) {
				val = (u32)((*(u32 *)buf & 0xFFFF0000) >> 16);
				hw_pci_config_rw(pdev, (char *)&val, 2, PCI_STATUS, iswrite);

				val = (u32)(*(u32 *)buf & 0x0000FFFF);
				*(u32 *)buf = val;
			}

			memcpy((u8 *)&nvdev->config_space[offset], buf, count);
			break;

		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_1:
		case PCI_BASE_ADDRESS_2:
		case PCI_BASE_ADDRESS_3:
		case PCI_BASE_ADDRESS_4:
			cfg_addr = *(u32 *)buf;

			switch (offset) {
			case PCI_BASE_ADDRESS_0:
				bar_mask = (u32)((~(pci_resource_len(pdev, VFIO_PCI_BAR0_REGION_INDEX)) + 1) & ~0xFul);
				cfg_addr = (cfg_addr & bar_mask) | (nvdev->config_space[offset] & 0xFul);
				break;
			case PCI_BASE_ADDRESS_1:
				bar_mask = (u32)((~(nvdev->curr_vgpu_type->bar1Length * 1024 * 1024) + 1) & ~0xFul);
				cfg_addr = (cfg_addr & bar_mask) | (nvdev->config_space[offset] & 0xFul);
				break;

			case PCI_BASE_ADDRESS_2:
				bar_mask = (u32)(((~(nvdev->curr_vgpu_type->bar1Length * 1024 * 1024) + 1) & ~0xFul) >> 32);
				cfg_addr = (cfg_addr & bar_mask);
				break;

			case PCI_BASE_ADDRESS_3:
				bar_mask = (u32)((~(pci_resource_len(pdev, VFIO_PCI_BAR3_REGION_INDEX)) + 1) & ~0xFul);
				cfg_addr = (cfg_addr & bar_mask) | (nvdev->config_space[offset] & 0xFul);
				break;

			case PCI_BASE_ADDRESS_4:
				bar_mask = (u32)(((~(pci_resource_len(pdev, VFIO_PCI_BAR3_REGION_INDEX)) + 1) & ~0xFul) >> 32);
				cfg_addr = (cfg_addr & bar_mask);
				break;
			}
			*(u32 *)&nvdev->config_space[offset] = cfg_addr;
			break;
		default:
			break;

		}
	}
	return count;
}

ssize_t nvidia_vgpu_vfio_access(struct nvidia_vgpu_vfio *nvdev, char *buf,
				size_t count, loff_t ppos, bool iswrite)
{
	int index = VFIO_PCI_OFFSET_TO_INDEX(ppos);

	if (index >= VFIO_PCI_NUM_REGIONS)
		return -EINVAL;

	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		return pci_config_rw(nvdev, buf, count, ppos,
				     iswrite);
	case VFIO_PCI_BAR0_REGION_INDEX:
		return bar0_rw(nvdev, buf, count, ppos, iswrite);
	default:
		return -EINVAL;
	}
	return count;
}
