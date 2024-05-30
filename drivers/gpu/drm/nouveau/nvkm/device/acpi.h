/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DEVICE_ACPI_H__
#define __NVKM_DEVICE_ACPI_H__
#include <core/os.h>
struct nvkm_device;

void nvkm_acpi_init(struct nvkm_device *);
void nvkm_acpi_fini(struct nvkm_device *);

#ifdef CONFIG_VGA_SWITCHEROO
extern struct nouveau_dsm_priv {
	bool dsm_detected;
	bool optimus_detected;
	bool optimus_flags_detected;
	bool optimus_skip_dsm;
	acpi_handle dhandle;
} nouveau_dsm_priv;

void nvkm_acpi_switcheroo_init(void);
void nvkm_acpi_switcheroo_fini(void);
void nvkm_acpi_switcheroo_set_powerdown(void);
#else
static inline void nvkm_acpi_switcheroo_init(void) {};
static inline void nvkm_acpi_switcheroo_fini(void) {};
static inline void nvkm_acpi_switcheroo_set_powerdown(void) {};
#endif

#endif
