// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/sizes.h>
#include <linux/vfio_pci_core.h>

struct cxl_device {
	struct vfio_pci_core_device core_device;
};

static int cxl_open_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl = vfio_pci_core_to_cxl(pci);
	struct vfio_cxl_dev_info info = {0};
	int ret;

	/* Driver reports the device DPA and RAM size */
	info.dpa_res = DEFINE_RES_MEM(0, SZ_256M);
	info.ram_res = DEFINE_RES_MEM_NAMED(0, SZ_256M, "ram");

	/* Initailize the CXL device and enable the vfio-pci-core */
	ret = vfio_cxl_core_enable(cxl, &info);
	if (ret)
		return ret;

	vfio_cxl_core_finish_enable(cxl);

	/* No precommitted region, create one. */
	if (!cxl->region.region) {
		/*
		 * Driver can choose to create cxl region at a certain time
		 * E.g. at driver initialization or later
		 */
		ret = vfio_cxl_core_create_cxl_region(cxl, SZ_256M);
		if (ret)
			goto fail_create_cxl_region;
	}

	ret = vfio_cxl_core_register_cxl_region(cxl);
	if (ret)
		goto fail_register_cxl_region;

	return 0;

fail_register_cxl_region:
	if (cxl->region.region)
		vfio_cxl_core_destroy_cxl_region(cxl);
fail_create_cxl_region:
	vfio_cxl_core_disable(cxl);
	return ret;
}

static void cxl_close_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *pci =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct vfio_cxl_core_device *cxl = vfio_pci_core_to_cxl(pci);

	vfio_cxl_core_unregister_cxl_region(cxl);
	vfio_cxl_core_destroy_cxl_region(cxl);
	vfio_cxl_core_disable(cxl);
	vfio_pci_core_close_device(vdev);
}

static const struct vfio_device_ops cxl_core_ops = {
	.name		= "cxl-vfio-pci",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device	= cxl_open_device,
	.close_device	= cxl_close_device,
	.ioctl		= vfio_cxl_core_ioctl,
	.device_feature	= vfio_pci_core_ioctl_feature,
	.read		= vfio_cxl_core_read,
	.write		= vfio_cxl_core_write,
	.mmap		= vfio_pci_core_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
	.detach_ioas	= vfio_iommufd_physical_detach_ioas,
};

static int cxl_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	const struct vfio_device_ops *ops = &cxl_core_ops;
	struct cxl_device *cxl_device;
	int ret;

	cxl_device = vfio_alloc_device(cxl_device, core_device.vdev,
				       &pdev->dev, ops);
	if (IS_ERR(cxl_device))
		return PTR_ERR(cxl_device);

	dev_set_drvdata(&pdev->dev, &cxl_device->core_device);

	ret = vfio_pci_core_register_device(&cxl_device->core_device);
	if (ret)
		goto out_put_vdev;

	return ret;

out_put_vdev:
	vfio_put_device(&cxl_device->core_device.vdev);
	return ret;
}

static void cxl_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_device = dev_get_drvdata(&pdev->dev);

	vfio_pci_core_unregister_device(core_device);
	vfio_put_device(&core_device->vdev);
}

static const struct pci_device_id cxl_vfio_pci_table[] = {
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_INTEL, 0xd94) },
	{}
};

MODULE_DEVICE_TABLE(pci, cxl_vfio_pci_table);

static struct pci_driver cxl_vfio_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = cxl_vfio_pci_table,
	.probe = cxl_probe,
	.remove = cxl_remove,
	.err_handler = &vfio_pci_core_err_handlers,
	.driver_managed_dma = true,
};

module_pci_driver(cxl_vfio_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhi Wang <zhiw@nvidia.com>");
MODULE_DESCRIPTION("VFIO variant driver for QEMU CXL accel device");
MODULE_IMPORT_NS("CXL");
