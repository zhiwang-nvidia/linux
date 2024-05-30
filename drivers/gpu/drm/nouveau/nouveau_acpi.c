// SPDX-License-Identifier: MIT
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <drm/drm_edid.h>
#include <acpi/video.h>

#include "nouveau_drv.h"
#include "nouveau_acpi.h"

void *
nouveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector)
{
	struct acpi_device *acpidev;
	int type, ret;
	void *edid;

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_eDP:
		type = ACPI_VIDEO_DISPLAY_LCD;
		break;
	default:
		return NULL;
	}

	acpidev = ACPI_COMPANION(dev->dev);
	if (!acpidev)
		return NULL;

	ret = acpi_video_get_edid(acpidev, type, -1, &edid);
	if (ret < 0)
		return NULL;

	return kmemdup(edid, EDID_LENGTH, GFP_KERNEL);
}

bool nouveau_acpi_video_backlight_use_native(void)
{
	return acpi_video_backlight_use_native();
}

void nouveau_acpi_video_register_backlight(void)
{
	acpi_video_register_backlight();
}
