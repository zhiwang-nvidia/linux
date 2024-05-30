/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_ACPI_H__
#define __NOUVEAU_ACPI_H__

#define ROM_BIOS_PAGE 4096

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
void *nouveau_acpi_edid(struct drm_device *, struct drm_connector *);
bool nouveau_acpi_video_backlight_use_native(void);
void nouveau_acpi_video_register_backlight(void);
#else
static inline void *nouveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector) { return NULL; }
static inline bool nouveau_acpi_video_backlight_use_native(void) { return true; }
static inline void nouveau_acpi_video_register_backlight(void) {}
#endif

#endif
