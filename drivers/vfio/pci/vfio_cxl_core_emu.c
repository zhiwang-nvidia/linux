// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "vfio_cxl_core_priv.h"

typedef ssize_t reg_handler_t(struct vfio_cxl_core_device *cxl, void *buf,
			      u64 offset, u64 size);

static struct vfio_emulated_regblock *
new_reg_block(struct vfio_cxl_core_device *cxl, u64 offset, u64 size,
	      reg_handler_t *read, reg_handler_t *write)
{
	struct vfio_emulated_regblock *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return ERR_PTR(-ENOMEM);

	block->range.start = offset;
	block->range.end = offset + size - 1;
	block->read = read;
	block->write = write;

	INIT_LIST_HEAD(&block->list);

	return block;
}

static int new_config_block(struct vfio_cxl_core_device *cxl, u64 offset,
			    u64 size, reg_handler_t *read, reg_handler_t *write)
{
	struct vfio_emulated_regblock *block;

	block = new_reg_block(cxl, offset, size, read, write);
	if (IS_ERR(block))
		return PTR_ERR(block);

	list_add_tail(&block->list, &cxl->config_regblocks_head);
	return 0;
}

static ssize_t virt_config_reg_read(struct vfio_cxl_core_device *cxl, void *buf,
				    u64 offset, u64 size)
{
	memcpy(buf, cxl->config_virt + offset, size);
	return size;
}

static ssize_t virt_config_reg_write(struct vfio_cxl_core_device *cxl, void *buf,
				     u64 offset, u64 size)
{
	memcpy(cxl->config_virt + offset, buf, size);
	return size;
}

static ssize_t hw_config_reg_read(struct vfio_cxl_core_device *cxl, void *buf,
				  u64 offset, u64 size)
{
	u32 val;
	int ret;

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	ret = pci_user_read_config_dword(cxl->pci_core.pdev, offset, &val);
	if (ret)
		return ret;

	val = cpu_to_le32(val);
	memcpy(buf, &val, size);

	return size;
}

static ssize_t hw_config_reg_write(struct vfio_cxl_core_device *cxl, void *buf,
				   u64 offset, u64 size)
{
	u32 val = le32_to_cpu(*(u32 *)buf);
	int ret;

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	ret = pci_user_write_config_dword(cxl->pci_core.pdev, offset, val);
	if (ret)
		return ret;

	return size;
}

static ssize_t cxl_control_write(struct vfio_cxl_core_device *cxl, void *buf,
				 u64 offset, u64 size)
{
	u16 lock = le16_to_cpu(*(u16 *)(cxl->config_virt + cxl->dvsec + 0x14));
	u16 cap3 = le16_to_cpu(*(u16 *)(cxl->config_virt + cxl->dvsec + 0x38));
	u16 new_val = le16_to_cpu(*(u16 *)buf);
	u16 rev_mask;

	if (WARN_ON_ONCE(size != 2))
		return -EINVAL;

	/* register is locked */
	if (lock & BIT(0))
		return size;

	/* handle reserved bits in the spec */
	rev_mask = BIT(13) | BIT(15);

	/* no direct p2p cap */
	if (!(cap3 & BIT(4)))
		rev_mask |= BIT(12);

	new_val &= ~rev_mask;

	/* CXL.io is always enabled. */
	new_val |= BIT(1);

	memcpy(cxl->config_virt + offset, &new_val, size);
	return size;
}

static ssize_t cxl_status_write(struct vfio_cxl_core_device *cxl, void *buf,
				u64 offset, u64 size)
{
	u16 cur_val = le16_to_cpu(*(u16 *)(cxl->config_virt + offset));
	u16 new_val = le16_to_cpu(*(u16 *)buf);
	u16 rev_mask = GENMASK(13, 0) | BIT(15);

	if (WARN_ON_ONCE(size != 2))
		return -EINVAL;

	/* handle reserved bits in the spec */
	new_val &= ~rev_mask;

	/* emulate RW1C bit */
	if (new_val & BIT(14)) {
		new_val &= ~BIT(14);
	} else {
		new_val &= ~BIT(14);
		new_val |= cur_val & BIT(14);
	}

	new_val = cpu_to_le16(new_val);
	memcpy(cxl->config_virt + offset, &new_val, size);
	return size;
}

static ssize_t cxl_control_2_write(struct vfio_cxl_core_device *cxl, void *buf,
				   u64 offset, u64 size)
{
	struct pci_dev *pdev = cxl->pci_core.pdev;
	u16 cap2 = le16_to_cpu(*(u16 *)(cxl->config_virt + cxl->dvsec + 0x16));
	u16 cap3 = le16_to_cpu(*(u16 *)(cxl->config_virt + cxl->dvsec + 0x38));
	u16 new_val = le16_to_cpu(*(u16 *)buf);
	u16 rev_mask = GENMASK(15, 6) | BIT(1) | BIT(2);
	u16 hw_bits = BIT(0) | BIT(1) | BIT(3);
	bool initiate_cxl_reset = new_val & BIT(2);

	if (WARN_ON_ONCE(size != 2))
		return -EINVAL;

	/* no desired volatile HDM state after host reset */
	if (!(cap3 & BIT(2)))
		rev_mask |= BIT(4);

	/* no modified completion enable */
	if (!(cap2 & BIT(6)))
		rev_mask |= BIT(5);

	/* handle reserved bits in the spec */
	new_val &= ~rev_mask;

	/* bits go to the HW */
	hw_bits &= new_val;

	/* update the virt regs */
	new_val = cpu_to_le16(new_val);
	memcpy(cxl->config_virt + offset, &new_val, size);

	if (hw_bits)
		pci_write_config_word(pdev, offset, cpu_to_le16(hw_bits));

	if (initiate_cxl_reset) {
		/* TODO: call linux CXL reset */
	}
	return size;
}

static ssize_t cxl_status_2_write(struct vfio_cxl_core_device *cxl, void *buf,
				  u64 offset, u64 size)
{
	struct pci_dev *pdev = cxl->pci_core.pdev;
	u16 cap3 = le16_to_cpu(*(u16 *)(cxl->config_virt + cxl->dvsec + 0x38));
	u16 new_val = le16_to_cpu(*(u16 *)buf);

	if (WARN_ON_ONCE(size != 2))
		return -EINVAL;

	/* write RW1CS if supports */
	if ((cap3 & BIT(2)) && (new_val & BIT(3)))
		pci_write_config_word(pdev, offset, BIT(3));

	/* No need to update the virt regs, CXL status reads from the HW */
	return size;
}

static ssize_t cxl_lock_write(struct vfio_cxl_core_device *cxl, void *buf,
			      u64 offset, u64 size)
{
	u16 cur_val = le16_to_cpu(*(u16 *)(cxl->config_virt + offset));
	u16 new_val = le16_to_cpu(*(u16 *)buf);
	u16 rev_mask = GENMASK(15, 1);

	if (WARN_ON_ONCE(size != 2))
		return -EINVAL;

	/* LOCK is not allowed to be cleared unless conventional reset. */
	if (cur_val & BIT(0))
		return size;

	/* handle reserved bits in the spec */
	new_val &= ~rev_mask;

	new_val = cpu_to_le16(new_val);
	memcpy(cxl->config_virt + offset, &new_val, size);
	return size;
}

static ssize_t cxl_base_lo_write(struct vfio_cxl_core_device *cxl, void *buf,
				 u64 offset, u64 size)
{
	u32 new_val = le32_to_cpu(*(u32 *)buf);
	u32 rev_mask = GENMASK(27, 0);

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	/* handle reserved bits in the spec */
	new_val &= ~rev_mask;

	new_val = cpu_to_le32(new_val);
	memcpy(cxl->config_virt + offset, &new_val, size);
	return size;
}

static ssize_t virt_config_reg_ro_write(struct vfio_cxl_core_device *cxl, void *buf,
					u64 offset, u64 size)
{
	return size;
}

static int setup_config_emulation(struct vfio_cxl_core_device *cxl)
{
	u16 offset;
	int ret;

#define ALLOC_BLOCK(offset, size, read, write) do { \
	ret = new_config_block(cxl, offset, size, read, write); \
	if (ret) \
		return ret; \
	} while (0)

	ALLOC_BLOCK(cxl->dvsec, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	ALLOC_BLOCK(cxl->dvsec + 0x4, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	ALLOC_BLOCK(cxl->dvsec + 0x8, 2,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	/* CXL CAPABILITY */
	ALLOC_BLOCK(cxl->dvsec + 0xa, 2,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	/* CXL CONTROL */
	ALLOC_BLOCK(cxl->dvsec + 0xc, 2,
			virt_config_reg_read,
			cxl_control_write);

	/* CXL STATUS */
	ALLOC_BLOCK(cxl->dvsec + 0xe, 2,
			virt_config_reg_read,
			cxl_status_write);

	/* CXL CONTROL 2 */
	ALLOC_BLOCK(cxl->dvsec + 0x10, 2,
			virt_config_reg_read,
			cxl_control_2_write);

	/* CXL STATUS 2 */
	ALLOC_BLOCK(cxl->dvsec + 0x12, 2,
			virt_config_reg_read,
			cxl_status_2_write);

	/* CXL LOCK */
	ALLOC_BLOCK(cxl->dvsec + 0x14, 2,
			virt_config_reg_read,
			cxl_lock_write);

	/* CXL CAPABILITY 2 */
	ALLOC_BLOCK(cxl->dvsec + 0x16, 2,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	/* CXL RANGE 1 SIZE HIGH & LOW */
	ALLOC_BLOCK(cxl->dvsec + 0x18, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	ALLOC_BLOCK(cxl->dvsec + 0x1c, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	/* CXL RANG BASE 1 HIGH */
	ALLOC_BLOCK(cxl->dvsec + 0x20, 4,
			virt_config_reg_read,
			virt_config_reg_write);

	/* CXL RANG BASE 1 LOW */
	ALLOC_BLOCK(cxl->dvsec + 0x24, 4,
			virt_config_reg_read,
			cxl_base_lo_write);

	/* CXL RANGE 2 SIZE HIGH & LOW */
	ALLOC_BLOCK(cxl->dvsec + 0x28, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	ALLOC_BLOCK(cxl->dvsec + 0x2c, 4,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	/* CXL RANG BASE 2 HIGH */
	ALLOC_BLOCK(cxl->dvsec + 0x30, 4,
			virt_config_reg_read,
			virt_config_reg_write);

	/* CXL RANG BASE 2 LOW */
	ALLOC_BLOCK(cxl->dvsec + 0x34, 4,
			virt_config_reg_read,
			cxl_base_lo_write);

	/* CXL CAPABILITY 3 */
	ALLOC_BLOCK(cxl->dvsec + 0x38, 2,
			virt_config_reg_read,
			virt_config_reg_ro_write);

	while ((offset = pci_find_next_ext_capability(cxl->pci_core.pdev, offset,
					PCI_EXT_CAP_ID_DOE))) {
		ALLOC_BLOCK(offset + PCI_DOE_CTRL, 4,
				hw_config_reg_read,
				hw_config_reg_write);

		ALLOC_BLOCK(offset + PCI_DOE_STATUS, 4,
				hw_config_reg_read,
				hw_config_reg_write);

		ALLOC_BLOCK(offset + PCI_DOE_WRITE, 4,
				hw_config_reg_read,
				hw_config_reg_write);

		ALLOC_BLOCK(offset + PCI_DOE_READ, 4,
				hw_config_reg_read,
				hw_config_reg_write);
	}

#undef ALLOC_BLOCK

	return 0;
}

static int new_mmio_block(struct vfio_cxl_core_device *cxl, u64 offset, u64 size,
			  reg_handler_t *read, reg_handler_t *write)
{
	struct vfio_emulated_regblock *block;

	block = new_reg_block(cxl, offset, size, read, write);
	if (IS_ERR(block))
		return PTR_ERR(block);

	list_add_tail(&block->list, &cxl->mmio_regblocks_head);
	return 0;
}

static u64 hdm_reg_base(struct vfio_cxl_core_device *cxl)
{
	return cxl->comp_reg_offset + cxl->hdm_reg_offset;
}

static u64 to_hdm_reg_offset(struct vfio_cxl_core_device *cxl, u64 offset)
{
	return offset - hdm_reg_base(cxl);
}

static void *hdm_reg_virt(struct vfio_cxl_core_device *cxl, u64 hdm_reg_offset)
{
	return cxl->comp_reg_virt + cxl->hdm_reg_offset + hdm_reg_offset;
}

static ssize_t virt_hdm_reg_read(struct vfio_cxl_core_device *cxl, void *buf,
				 u64 offset, u64 size)
{
	offset = to_hdm_reg_offset(cxl, offset);
	memcpy(buf, hdm_reg_virt(cxl, offset), size);

	return size;
}

static ssize_t virt_hdm_reg_write(struct vfio_cxl_core_device *cxl, void *buf,
				  u64 offset, u64 size)
{
	offset = to_hdm_reg_offset(cxl, offset);
	memcpy(hdm_reg_virt(cxl, offset), buf, size);

	return size;
}

static ssize_t virt_hdm_rev_reg_write(struct vfio_cxl_core_device *cxl,
				      void *buf, u64 offset, u64 size)
{
	/* Discard writes on reserved registers. */
	return size;
}

static ssize_t hdm_decoder_n_lo_write(struct vfio_cxl_core_device *cxl,
				      void *buf, u64 offset, u64 size)
{
	u32 new_val = le32_to_cpu(*(u32 *)buf);

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	/* Bit [27:0] are reserved. */
	new_val &= ~GENMASK(27, 0);

	new_val = cpu_to_le32(new_val);
	offset = to_hdm_reg_offset(cxl, offset);
	memcpy(hdm_reg_virt(cxl, offset), &new_val, size);
	return size;
}

static ssize_t hdm_decoder_global_ctrl_write(struct vfio_cxl_core_device *cxl,
					     void *buf, u64 offset, u64 size)
{
	u32 hdm_decoder_global_cap;
	u32 new_val = le32_to_cpu(*(u32 *)buf);

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	/* Bit [31:2] are reserved. */
	new_val &= ~GENMASK(31, 2);

	/* Poison On Decode Error Enable bit is 0 and RO if not support. */
	hdm_decoder_global_cap = le32_to_cpu(*(u32 *)hdm_reg_virt(cxl, 0));
	if (!(hdm_decoder_global_cap & BIT(10)))
		new_val &= ~BIT(0);

	new_val = cpu_to_le32(new_val);
	offset = to_hdm_reg_offset(cxl, offset);
	memcpy(hdm_reg_virt(cxl, offset), &new_val, size);
	return size;
}

static ssize_t hdm_decoder_n_ctrl_write(struct vfio_cxl_core_device *cxl,
					void *buf, u64 offset, u64 size)
{
	u32 hdm_decoder_global_cap;
	u32 ro_mask, rev_mask;
	u32 new_val = le32_to_cpu(*(u32 *)buf);
	u32 cur_val;

	if (WARN_ON_ONCE(size != 4))
		return -EINVAL;

	offset = to_hdm_reg_offset(cxl, offset);
	cur_val = le32_to_cpu(*(u32 *)hdm_reg_virt(cxl, offset));

	/* Lock on commit */
	if (cur_val & BIT(8))
		return size;

	hdm_decoder_global_cap = le32_to_cpu(*(u32 *)hdm_reg_virt(cxl, 0));

	/* RO and reserved bits in the spec */
	ro_mask = BIT(10) | BIT(11);
	rev_mask = BIT(15) | GENMASK(31, 28);

	/* bits are not valid for devices */
	ro_mask |= BIT(12);
	rev_mask |= GENMASK(19, 16) | GENMASK(23, 20);

	/* bits are reserved when UIO is not supported */
	if (!(hdm_decoder_global_cap & BIT(13)))
		rev_mask |= BIT(14) | GENMASK(27, 24);

	/* clear reserved bits */
	new_val &= ~rev_mask;

	/* keep the RO bits */
	cur_val &= ro_mask;
	new_val &= ~ro_mask;
	new_val |= cur_val;

	new_val = cpu_to_le32(new_val);
	memcpy(hdm_reg_virt(cxl, offset), &new_val, size);
	return size;
}

static int setup_mmio_emulation(struct vfio_cxl_core_device *cxl)
{
	u64 offset, base;
	int ret;

	base = hdm_reg_base(cxl);

#define ALLOC_BLOCK(offset, size, read, write) do { \
	ret = new_mmio_block(cxl, offset, size, read, write); \
	if (ret) \
		return ret; \
	} while (0)

	ALLOC_BLOCK(base + 0x4, 4,
			virt_hdm_reg_read,
			hdm_decoder_global_ctrl_write);

	offset = base + 0x10;
	while (offset < base + cxl->hdm_reg_size) {
		/* HDM N BASE LOW */
		ALLOC_BLOCK(offset, 4,
				virt_hdm_reg_read,
				hdm_decoder_n_lo_write);

		/* HDM N BASE HIGH */
		ALLOC_BLOCK(offset + 0x4, 4,
				virt_hdm_reg_read,
				virt_hdm_reg_write);

		/* HDM N SIZE LOW */
		ALLOC_BLOCK(offset + 0x8, 4,
				virt_hdm_reg_read,
				hdm_decoder_n_lo_write);

		/* HDM N SIZE HIGH */
		ALLOC_BLOCK(offset + 0xc, 4,
				virt_hdm_reg_read,
				virt_hdm_reg_write);

		/* HDM N CONTROL */
		ALLOC_BLOCK(offset + 0x10, 4,
				virt_hdm_reg_read,
				hdm_decoder_n_ctrl_write);

		/* HDM N TARGET LIST LOW */
		ALLOC_BLOCK(offset + 0x14, 0x4,
				virt_hdm_reg_read,
				virt_hdm_rev_reg_write);

		/* HDM N TARGET LIST HIGH */
		ALLOC_BLOCK(offset + 0x18, 0x4,
				virt_hdm_reg_read,
				virt_hdm_rev_reg_write);

		/* HDM N REV */
		ALLOC_BLOCK(offset + 0x1c, 0x4,
				virt_hdm_reg_read,
				virt_hdm_rev_reg_write);

		offset += 0x20;
	}

#undef ALLOC_BLOCK
	return 0;
}

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
	int ret;

	INIT_LIST_HEAD(&cxl->config_regblocks_head);
	INIT_LIST_HEAD(&cxl->mmio_regblocks_head);

	ret = setup_config_emulation(cxl);
	if (ret)
		goto err;

	ret = setup_mmio_emulation(cxl);
	if (ret)
		goto err;

	return 0;
err:
	vfio_cxl_core_clean_register_emulation(cxl);
	return ret;
}

static struct vfio_emulated_regblock *
find_regblock(struct list_head *head, u64 offset, u64 size)
{
	struct vfio_emulated_regblock *block;
	struct list_head *pos;

	list_for_each(pos, head) {
		block = list_entry(pos, struct vfio_emulated_regblock, list);

		if (block->range.start == ALIGN_DOWN(offset,
					range_len(&block->range)))
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
