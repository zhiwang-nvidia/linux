// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */


#include <linux/pci.h>
#include <linux/cxl_accel_mem.h>
#include <linux/cxl_accel_pci.h>

#include "net_driver.h"
#include "efx_cxl.h"

#define EFX_CTPIO_BUFFER_SIZE	(1024*1024*256)

void efx_cxl_init(struct efx_nic *efx)
{
	struct pci_dev *pci_dev = efx->pci_dev;
	resource_size_t start, end, max = 0;
	struct efx_cxl *cxl = efx->cxl;
	struct resource res;
	u16 dvsec;

	dvsec = pci_find_dvsec_capability(pci_dev, PCI_VENDOR_ID_CXL,
					  CXL_DVSEC_PCIE_DEVICE);

	if (!dvsec)
		return;

	pci_info(pci_dev, "CXL CXL_DVSEC_PCIE_DEVICE capability found");

	cxl->cxlds = cxl_accel_state_create(&pci_dev->dev,
					    CXL_ACCEL_DRIVER_CAP_HDM);
	if (IS_ERR(cxl->cxlds)) {
		pci_info(pci_dev, "CXL accel device state failed");
		return;
	}

	cxl_accel_set_dvsec(cxl->cxlds, dvsec);
	cxl_accel_set_serial(cxl->cxlds, pci_dev->dev.id);

	res = DEFINE_RES_MEM(0, EFX_CTPIO_BUFFER_SIZE);
	cxl_accel_set_resource(cxl->cxlds, res, CXL_ACCEL_RES_DPA);

	res = DEFINE_RES_MEM_NAMED(0, EFX_CTPIO_BUFFER_SIZE, "ram");
	cxl_accel_set_resource(cxl->cxlds, res, CXL_ACCEL_RES_RAM);

	if (cxl_pci_accel_setup_regs(pci_dev, cxl->cxlds)) {
		pci_info(pci_dev, "CXL accel setup regs failed");
		return;
	}

	if (cxl_accel_request_resource(cxl->cxlds, true))
		pci_info(pci_dev, "CXL accel resource request failed");

	if (!cxl_await_media_ready(cxl->cxlds)) {
		cxl_accel_set_media_ready(cxl->cxlds);
	} else {
		pci_info(pci_dev, "CXL accel media not active");
		return;
	}

	cxl->cxlmd = devm_cxl_add_memdev(&pci_dev->dev, cxl->cxlds);
	if (IS_ERR(cxl->cxlmd)) {
		pci_info(pci_dev, "CXL accel memdev creation failed");
		return;
	}

	cxl->endpoint = cxl_acquire_endpoint(cxl->cxlmd);
	if (IS_ERR(cxl->endpoint))
		pci_info(pci_dev, "CXL accel acquire endpoint failed");

	cxl->cxlrd = cxl_get_hpa_freespace(cxl->endpoint, 1,
					    CXL_DECODER_F_RAM | CXL_DECODER_F_TYPE2,
					    &max);

	if (IS_ERR(cxl->cxlrd)) {
		pci_info(pci_dev, "CXL accel get HPA failed");
		goto out;
	}

	if (max < EFX_CTPIO_BUFFER_SIZE) {
		pci_info(pci_dev, "CXL accel not enough free HPA space %llu < %u\n",
				  max, EFX_CTPIO_BUFFER_SIZE);
		goto out;
	}

	cxl->cxled = cxl_request_dpa(cxl->endpoint, true, EFX_CTPIO_BUFFER_SIZE,
				     EFX_CTPIO_BUFFER_SIZE);
	if (IS_ERR(cxl->cxled)) {
		pci_info(pci_dev, "CXL accel request DPA failed");
		return;
	}

	cxl->efx_region = cxl_create_region(cxl->cxlrd, &cxl->cxled, 1);
	if (!cxl->efx_region) {
		pci_info(pci_dev, "CXL accel create region failed");
		cxl_dpa_free(cxl->cxled);
		return;
	}

	cxl_accel_get_region_params(cxl->efx_region, &start, &end);

	cxl->ctpio_cxl = ioremap(start, end - start);
	if (!cxl->ctpio_cxl) {
		pci_info(pci_dev, "CXL accel create region failed");
		cxl_dpa_free(cxl->cxled);
	}
out:
	cxl_release_endpoint(cxl->cxlmd, cxl->endpoint);
}

void efx_cxl_exit(struct efx_nic *efx)
{
	struct efx_cxl *cxl = efx->cxl;

	if (cxl->ctpio_cxl)
		iounmap(cxl->ctpio_cxl);

	if (cxl->efx_region)
		cxl_region_detach(cxl->cxled);

	if (cxl->cxled)
		cxl_dpa_free(cxl->cxled);

	return;
 }

MODULE_IMPORT_NS(CXL);
