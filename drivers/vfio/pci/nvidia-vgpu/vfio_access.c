// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/string.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "vfio.h"

#define vconfig_set8(offset, v) \
	(*(u8 *)(nvdev->vconfig + (offset)) = v)

#define vconfig_set16(offset, v) \
	(*(u16 *)(nvdev->vconfig + (offset)) = v)

#define vconfig_set32(offset, v) \
	(*(u32 *)(nvdev->vconfig + (offset)) = v)

void nvidia_vgpu_vfio_setup_config(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_type *vgpu_type;
	u8 val8;

	lockdep_assert_held(&nvdev->vfio_vgpu_lock);

	if (WARN_ON(!nvdev->vgpu_type))
		return;

	vgpu_type = nvdev->vgpu_type;

	memset(nvdev->vconfig, 0, sizeof(nvdev->vconfig));

	/* Header type 0 (normal devices) */
	vconfig_set16(PCI_VENDOR_ID, PCI_VENDOR_ID_NVIDIA);
	vconfig_set16(PCI_DEVICE_ID, FIELD_GET(GENMASK(31, 16), vgpu_type->vdev_id));
	vconfig_set16(PCI_COMMAND, 0x0000);
	vconfig_set16(PCI_STATUS, 0x0010);

	pci_read_config_byte(nvdev->core_dev.pdev, PCI_CLASS_REVISION, &val8);
	vconfig_set8(PCI_CLASS_REVISION, val8);

	vconfig_set8(PCI_CLASS_PROG, 0); /* VGA-compatible */
	vconfig_set8(PCI_CLASS_DEVICE, 0); /* VGA controller */
	vconfig_set8(PCI_CLASS_DEVICE + 1, 3); /* Display controller */

	/* BAR0: 32-bit */
	vconfig_set32(PCI_BASE_ADDRESS_0, 0x00000000);
	/* BAR1: 64-bit, prefetchable */
	vconfig_set32(PCI_BASE_ADDRESS_1, 0x0000000c);
	/* BAR2: 64-bit, prefetchable */
	vconfig_set32(PCI_BASE_ADDRESS_3, 0x0000000c);
	/* Disable BAR3: I/O */
	vconfig_set32(PCI_BASE_ADDRESS_5, 0x00000000);

	vconfig_set16(PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_NVIDIA);
	vconfig_set16(PCI_SUBSYSTEM_ID, FIELD_GET(GENMASK(15, 0),
		      nvdev->vgpu->info.vgpu_type->vdev_id));

	vconfig_set8(PCI_CAPABILITY_LIST, CAP_LIST_NEXT_PTR_MSIX);
	vconfig_set8(CAP_LIST_NEXT_PTR_MSIX + 1, 0);

	/* INTx disabled */
	vconfig_set8(0x3d, 0);
}

#define PCI_CONFIG_READ(pdev, off, buf, size) \
	do { \
		switch (size) { \
		case 4: pci_read_config_dword((pdev), (off), (u32 *)(buf)); break; \
		case 2: pci_read_config_word((pdev), (off), (u16 *)(buf)); break; \
		case 1: pci_read_config_byte((pdev), (off), (u8 *)(buf));  break; \
		} \
	} while (0)

#define PCI_CONFIG_WRITE(pdev, off, buf, size) \
	do { \
		switch (size) { \
		case 4: pci_write_config_dword((pdev), (off), *(u32 *)(buf)); break; \
		case 2: pci_write_config_word((pdev), (off), *(u16 *)(buf)); break; \
		case 1: pci_write_config_byte((pdev), (off), *(u8 *)(buf));  break; \
		} \
	} while (0)

#define MMIO_READ(map, off, buf, size) \
	do { \
		switch (size) { \
		case 4: { u32 val = ioread32((map) + (off)); memcpy((buf), &val, 4); break; } \
		case 2: { u16 val = ioread16((map) + (off)); memcpy((buf), &val, 2); break; } \
		case 1: { u8  val = ioread8((map) + (off)); memcpy((buf), &val, 1); break; } \
		} \
	} while (0)

#define MMIO_WRITE(map, off, buf, size) \
	do { \
		switch (size) { \
		case 4: iowrite32(*(u32 *)(buf), (map) + (off)); break; \
		case 2: iowrite16(*(u16 *)(buf), (map) + (off)); break; \
		case 1: iowrite8 (*(u8  *)(buf), (map) + (off)); break; \
		} \
	} while (0)

static ssize_t bar0_rw(struct nvidia_vgpu_vfio *nvdev, char *buf, size_t count, loff_t ppos,
		       bool iswrite)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	int index = VFIO_PCI_OFFSET_TO_INDEX(ppos);
	loff_t offset = ppos;
	void __iomem *map;
	int ret;

	if (WARN_ON(index != VFIO_PCI_BAR0_REGION_INDEX))
		return -EINVAL;

	offset &= VFIO_PCI_OFFSET_MASK;

	if (!nvdev->bar0_map) {
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
	} else {
		map = nvdev->bar0_map;
	}

	if (iswrite)
		MMIO_WRITE(map, offset, buf, count);
	else
		MMIO_READ(map, offset, buf, count);

	return count;
}

/* Generate mask for 32-bit or 64-bit PCI BAR address range */
#define GEN_BARMASK(size)        ((u32)((~(size) + 1) & ~0xFUL))
#define GEN_BARMASK_HI(size)     ((u32)(((~(size) + 1) & ~0xFULL) >> 32))

static u32 emulate_pci_base_reg_write(struct nvidia_vgpu_vfio *nvdev, loff_t offset, u32 cfg_addr)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	struct nvidia_vgpu_type *vgpu_type = nvdev->vgpu->info.vgpu_type;
	u32 bar_mask;

	switch (offset) {
	case PCI_BASE_ADDRESS_0:
		bar_mask = GEN_BARMASK(pci_resource_len(pdev, VFIO_PCI_BAR0_REGION_INDEX));
		cfg_addr = (cfg_addr & bar_mask) | (nvdev->vconfig[offset] & 0xFUL);
		break;

	case PCI_BASE_ADDRESS_1:
		bar_mask = GEN_BARMASK(vgpu_type->bar1_length * SZ_1M);
		cfg_addr = (cfg_addr & bar_mask) | (nvdev->vconfig[offset] & 0xFUL);
		break;

	case PCI_BASE_ADDRESS_2:
		bar_mask = GEN_BARMASK_HI(vgpu_type->bar1_length * SZ_1M);
		cfg_addr &= bar_mask;
		break;

	case PCI_BASE_ADDRESS_3:
		bar_mask = GEN_BARMASK(pci_resource_len(pdev, VFIO_PCI_BAR3_REGION_INDEX));
		cfg_addr = (cfg_addr & bar_mask) | (nvdev->vconfig[offset] & 0xFUL);
		break;

	case PCI_BASE_ADDRESS_4:
		bar_mask = GEN_BARMASK_HI(pci_resource_len(pdev, VFIO_PCI_BAR3_REGION_INDEX));
		cfg_addr &= bar_mask;
		break;

	default:
		WARN_ONCE(1, "Unsupported PCI BAR offset: %llx\n", offset);
		return 0;
	}

	return cfg_addr;
}

static void handle_pci_config_read(struct nvidia_vgpu_vfio *nvdev, char *buf,
				   size_t count, loff_t offset)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	u32 val = 0;

	memcpy(buf, (u8 *)&nvdev->vconfig[offset], count);

	switch (offset) {
	case PCI_COMMAND:
		PCI_CONFIG_READ(pdev, offset, (char *)&val, count);

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
		PCI_CONFIG_READ(pdev, offset, (char *)&val, count);
		break;
	default:
		break;
	}
	*(u32 *)buf = *(u32 *)buf | val;
}

static void handle_pci_config_write(struct nvidia_vgpu_vfio *nvdev, char *buf,
				    size_t count, loff_t offset)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	u32 val = 0;
	u32 cfg_addr;

	switch (offset) {
	case PCI_VENDOR_ID:
	case PCI_DEVICE_ID:
	case PCI_CAPABILITY_LIST:
		break;

	case PCI_STATUS:
		PCI_CONFIG_WRITE(pdev, offset, buf, count);
		break;
	case PCI_COMMAND:
		if (count == 4) {
			val = (u32)((*(u32 *)buf & 0xFFFF0000) >> 16);
			PCI_CONFIG_WRITE(pdev, PCI_STATUS, (char *)&val, 2);

			val = (u32)(*(u32 *)buf & 0x0000FFFF);
			*(u32 *)buf = val;
		}

		memcpy((u8 *)&nvdev->vconfig[offset], buf, count);
		break;
	case PCI_BASE_ADDRESS_0:
	case PCI_BASE_ADDRESS_1:
	case PCI_BASE_ADDRESS_2:
	case PCI_BASE_ADDRESS_3:
	case PCI_BASE_ADDRESS_4:
		cfg_addr = *(u32 *)buf;
		cfg_addr = emulate_pci_base_reg_write(nvdev, offset, cfg_addr);
		*(u32 *)&nvdev->vconfig[offset] = cfg_addr;
		break;
	default:
		break;
	}
}

static ssize_t pci_config_rw(struct nvidia_vgpu_vfio *nvdev, char *buf, size_t count,
			     loff_t ppos, bool iswrite)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;
	int index = VFIO_PCI_OFFSET_TO_INDEX(ppos);
	loff_t offset = ppos;

	if (WARN_ON(index != VFIO_PCI_CONFIG_REGION_INDEX))
		return -EINVAL;

	offset &= VFIO_PCI_OFFSET_MASK;

	if (offset >= CAP_LIST_NEXT_PTR_MSIX &&
	    offset < CAP_LIST_NEXT_PTR_MSIX + MSIX_CAP_SIZE) {
		if (!iswrite)
			PCI_CONFIG_READ(pdev, offset, buf, count);
		else
			PCI_CONFIG_WRITE(pdev, offset, buf, count);
		return count;
	}

	if (!iswrite)
		handle_pci_config_read(nvdev, buf, count, offset);
	else
		handle_pci_config_write(nvdev, buf, count, offset);

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
