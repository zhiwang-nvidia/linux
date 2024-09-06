// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/sizes.h>
#include <linux/vfio_pci_core.h>

struct cxl_device {
	struct vfio_pci_core_device core_device;
};

static int cxl_open_device(struct vfio_device *vdev)
{
	struct vfio_pci_core_device *core_dev =
		container_of(vdev, struct vfio_pci_core_device, vdev);
	struct resource res;
	int ret;

	/* Provide the device infomation to the kernel CXL core.*/
	/* Device DPA */
	res = DEFINE_RES_MEM(0, SZ_256M);
	vfio_cxl_core_set_resource(core_dev, res, CXL_ACCEL_RES_DPA);

	/* Device RAM */
	res = DEFINE_RES_MEM_NAMED(0, SZ_256M, "ram");
	vfio_cxl_core_set_resource(core_dev, res, CXL_ACCEL_RES_RAM);

	/* The expected size of the CXL region to be created */
	vfio_cxl_core_set_region_size(core_dev, SZ_256M);
	vfio_cxl_core_set_driver_hdm_cap(core_dev);

	/* Initailize the CXL device and enable the vfio-pci-core */
	ret = vfio_cxl_core_enable(core_dev);
	if (ret)
		return ret;

	vfio_cxl_core_finish_enable(core_dev);

	return 0;
}

static const struct vfio_device_ops cxl_core_ops = {
	.name		= "cxl-vfio-pci",
	.init		= vfio_pci_core_init_dev,
	.release	= vfio_pci_core_release_dev,
	.open_device	= cxl_open_device,
	.close_device	= vfio_cxl_core_close_device,
	.ioctl		= vfio_pci_core_ioctl,
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
MODULE_IMPORT_NS(CXL);
