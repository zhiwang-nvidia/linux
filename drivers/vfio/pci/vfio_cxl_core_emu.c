// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "vfio_cxl_core_priv.h"

void vfio_cxl_core_clean_register_emulation(struct vfio_cxl_core_device *cxl)
{
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &cxl->config_regblocks_head)
		kfree(list_entry(pos, struct vfio_emulated_regblock, list));
	list_for_each_safe(pos, n, &cxl->mmio_regblocks_head)
		kfree(list_entry(pos, struct vfio_emulated_regblock, list));
}

int vfio_cxl_core_setup_register_emulation(struct vfio_cxl_core_device *cxl)
{
	INIT_LIST_HEAD(&cxl->config_regblocks_head);
	INIT_LIST_HEAD(&cxl->mmio_regblocks_head);

	return 0;
}

static struct vfio_emulated_regblock *
find_regblock(struct list_head *head, u64 offset, u64 size)
{
	struct vfio_emulated_regblock *block;
	struct range expected;
	struct list_head *pos;

	expected.start = offset;
	expected.end = offset + size - 1;

	list_for_each(pos, head) {
		block = list_entry(pos, struct vfio_emulated_regblock, list);

		if (range_overlaps(&expected, &block->range))
			return block;
	}
	return NULL;
}

static ssize_t emulate_read(struct list_head *head, struct vfio_device *vdev,
			    char __user *buf, size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	struct vfio_emulated_regblock *block;
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	ssize_t ret;
	u32 v;

	block = find_regblock(head, pos, count);
	if (!block || !block->read)
		return vfio_pci_rw(pci, buf, count, ppos, false);

	if (WARN_ON_ONCE(!IS_ALIGNED(pos, range_len(&block->range))))
		return -EINVAL;

	if (count > range_len(&block->range))
		count = range_len(&block->range);

	ret = block->read(cxl, &v, pos, count);
	if (ret < 0)
		return ret;

	if (copy_to_user(buf, &v, count))
		return -EFAULT;

	return count;
}

static ssize_t emulate_write(struct list_head *head, struct vfio_device *vdev,
			    char __user *buf, size_t count, loff_t *ppos)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	struct vfio_emulated_regblock *block;
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	ssize_t ret;
	u32 v;

	block = find_regblock(head, pos, count);
	if (!block || !block->write)
		return vfio_pci_rw(pci, buf, count, ppos, true);

	if (WARN_ON_ONCE(!IS_ALIGNED(pos, range_len(&block->range))))
		return -EINVAL;

	if (count > range_len(&block->range))
		count = range_len(&block->range);

	if (copy_from_user(&v, buf, count))
		return -EFAULT;

	ret = block->write(cxl, &v, pos, count);
	if (ret < 0)
		return ret;

	return count;
}

ssize_t vfio_cxl_core_config_rw(struct vfio_device *vdev, char __user *buf,
		size_t count, loff_t *ppos, bool write)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	size_t done = 0;
	ssize_t ret = 0;
	loff_t tmp, pos = *ppos;

	while (count) {
		tmp = pos;

		if (count >= 4 && IS_ALIGNED(pos, 4))
			ret = 4;
		else if (count >= 2 && IS_ALIGNED(pos, 2))
			ret = 2;
		else
			ret = 1;

		if (write)
			ret = emulate_write(&cxl->config_regblocks_head,
					    vdev, buf, ret, &tmp);
		else
			ret = emulate_read(&cxl->config_regblocks_head,
					   vdev, buf, ret, &tmp);
		if (ret < 0)
			return ret;

		count -= ret;
		done += ret;
		buf += ret;
		pos += ret;
	}

	*ppos += done;
	return done;
}

ssize_t vfio_cxl_core_mmio_bar_rw(struct vfio_device *vdev, char __user *buf,
		size_t count, loff_t *ppos, bool write)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl =
		container_of(pci, struct vfio_cxl_core_device, pci_core);
	size_t done = 0;
	ssize_t ret = 0;
	loff_t tmp, pos = *ppos;

	while (count) {
		tmp = pos;

		if (count >= 4 && IS_ALIGNED(pos, 4))
			ret = 4;
		else if (count >= 2 && IS_ALIGNED(pos, 2))
			ret = 2;
		else
			ret = 1;

		if (write)
			ret = emulate_write(&cxl->mmio_regblocks_head,
					    vdev, buf, ret, &tmp);
		else
			ret = emulate_read(&cxl->mmio_regblocks_head,
					   vdev, buf, ret, &tmp);
		if (ret < 0)
			return ret;

		count -= ret;
		done += ret;
		buf += ret;
		pos += ret;
	}

	*ppos += done;
	return done;
}
