// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vfio_pci_core.h>
#include <linux/types.h>

#include "vfio.h"

static struct nvidia_vgpu_type *find_vgpu_type(struct nvidia_vgpu_vfio *nvdev, u64 type_id)
{
	struct nvidia_vgpu_type *vgpu_type;
	unsigned int i;

	for (i = 0; i < nvdev->vgpu_mgr->num_vgpu_types; i++) {
		vgpu_type = nvdev->vgpu_mgr->vgpu_types + i;
		if (vgpu_type->vgpu_type == type_id)
			return vgpu_type;
	}
	return NULL;
}

static ssize_t creatable_homogeneous_vgpu_types_show(struct nvidia_vgpu_vfio *nvdev, char *buf)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;
	ssize_t ret = 0;
	u64 i;

	mutex_lock(&vgpu_mgr->curr_vgpu_type_lock);
	/* No vGPU has been created. */
	if (!vgpu_mgr->curr_vgpu_type) {
		ret += sprintf(buf, "ID    : vGPU Name\n");

		for (i = 0; i < vgpu_mgr->num_vgpu_types; i++) {
			struct nvidia_vgpu_type *type = vgpu_mgr->vgpu_types + i;

			ret += sprintf(buf + ret, "%-5d : %s\n", type->vgpu_type,
				       type->vgpu_type_name);
		}
	} else {
		struct nvidia_vgpu_type *type = vgpu_mgr->curr_vgpu_type;

		/* There has been created vGPU(s). */
		if (vgpu_mgr->num_instances < type->max_instance)
			ret = sprintf(buf + ret, "%-5d : %s\n", type->vgpu_type,
				      type->vgpu_type_name);
	}
	mutex_unlock(&vgpu_mgr->curr_vgpu_type_lock);
	return ret;
}

static int create_homogeneous_instance(struct nvidia_vgpu_vfio *nvdev,
				       struct nvidia_vgpu_type *type)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;
	int ret = 0;

	mutex_lock(&vgpu_mgr->curr_vgpu_type_lock);
	if (!vgpu_mgr->curr_vgpu_type) {
		vgpu_mgr->curr_vgpu_type = type;
		vgpu_mgr->num_instances++;
		nvdev->vgpu_type = type;
	} else {
		if (type != vgpu_mgr->curr_vgpu_type) {
			ret = -EINVAL;
		} else if (vgpu_mgr->num_instances >= vgpu_mgr->curr_vgpu_type->max_instance) {
			ret = -ENOSPC;
		} else {
			vgpu_mgr->num_instances++;
			nvdev->vgpu_type = type;
		}
	}
	mutex_unlock(&vgpu_mgr->curr_vgpu_type_lock);
	return ret;
}

static void destroy_homogeneous_instance(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;

	if (!nvdev->vgpu_type)
		return;

	mutex_lock(&vgpu_mgr->curr_vgpu_type_lock);
	if (vgpu_mgr->curr_vgpu_type) {
		if (!--vgpu_mgr->num_instances)
			vgpu_mgr->curr_vgpu_type = NULL;
	}
	nvdev->vgpu_type = NULL;
	mutex_unlock(&vgpu_mgr->curr_vgpu_type_lock);
}

static ssize_t creatable_vgpu_types_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vfio_pci_core_device *core_dev = pci_get_drvdata(pdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	ssize_t ret;

	mutex_lock(&nvdev->vfio_vgpu_lock);
	if (nvdev->vgpu_type) {
		mutex_unlock(&nvdev->vfio_vgpu_lock);
		return 0;
	}

	ret = creatable_homogeneous_vgpu_types_show(nvdev, buf);
	mutex_unlock(&nvdev->vfio_vgpu_lock);
	return ret;
}

static DEVICE_ATTR_RO(creatable_vgpu_types);

static ssize_t current_vgpu_type_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vfio_pci_core_device *core_dev = pci_get_drvdata(pdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	struct nvidia_vgpu_type *type;
	unsigned long vgpu_type_id = ~0;
	int ret = 0;

	ret = kstrtoul(buf, 10, &vgpu_type_id);
	if (ret)
		return ret;

	mutex_lock(&nvdev->vfio_vgpu_lock);

	if (nvdev->vdev_is_opened) {
		mutex_unlock(&nvdev->vfio_vgpu_lock);
		return -EBUSY;
	}

	if (vgpu_type_id) {
		type = find_vgpu_type(nvdev, vgpu_type_id);
		if (!type) {
			ret = -ENODEV;
			goto out_unlock;
		}
		ret = create_homogeneous_instance(nvdev, type);
	} else {
		destroy_homogeneous_instance(nvdev);
	}

out_unlock:
	mutex_unlock(&nvdev->vfio_vgpu_lock);
	return ret ? ret : count;
}

static ssize_t current_vgpu_type_show(struct device *dev, struct device_attribute *attr,
				      char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vfio_pci_core_device *core_dev = pci_get_drvdata(pdev);
	struct nvidia_vgpu_vfio *nvdev = core_dev_to_nvdev(core_dev);
	unsigned long type_id;

	mutex_lock(&nvdev->vfio_vgpu_lock);

	type_id = nvdev->vgpu_type ? nvdev->vgpu_type->vgpu_type : 0;

	mutex_unlock(&nvdev->vfio_vgpu_lock);

	return sprintf(buf, "%lu\n", type_id);
}

static DEVICE_ATTR_RW(current_vgpu_type);

static struct attribute *vf_dev_attrs[] = {
	&dev_attr_creatable_vgpu_types.attr,
	&dev_attr_current_vgpu_type.attr,
	NULL,
};

static const struct attribute_group vf_dev_group = {
	.name  = "nvidia",
	.attrs = vf_dev_attrs,
};

const struct attribute_group *vf_dev_groups[] = {
	&vf_dev_group,
	NULL,
};

int nvidia_vgpu_vfio_setup_sysfs(struct nvidia_vgpu_vfio *nvdev)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;

	if (WARN_ON(!pdev))
		return -EINVAL;

	return sysfs_create_groups(&pdev->dev.kobj, vf_dev_groups);
}

void nvidia_vgpu_vfio_clean_sysfs(struct nvidia_vgpu_vfio *nvdev)
{
	struct pci_dev *pdev = nvdev->core_dev.pdev;

	if (WARN_ON(!pdev))
		return;

	sysfs_remove_groups(&pdev->dev.kobj, vf_dev_groups);
}
