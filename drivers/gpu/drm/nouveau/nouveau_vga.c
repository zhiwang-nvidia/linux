// SPDX-License-Identifier: MIT
#include <linux/vga_switcheroo.h>

#include <drm/drm_fb_helper.h>

#include "nouveau_drv.h"
#include "nouveau_acpi.h"
#include "nouveau_vga.h"

static void
nouveau_switcheroo_set_state(const struct nvif_driver_func *driver, enum vga_switcheroo_state state)
{
	struct drm_device *dev = container_of(driver, struct nouveau_drm, driver)->dev;

	if (state == VGA_SWITCHEROO_ON) {
		pr_err("VGA switcheroo: switched nouveau on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		nouveau_pmops_resume(dev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("VGA switcheroo: switched nouveau off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		nouveau_pmops_suspend(dev->dev);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static void
nouveau_switcheroo_reprobe(const struct nvif_driver_func *driver)
{
	struct drm_device *dev = container_of(driver, struct nouveau_drm, driver)->dev;

	drm_fb_helper_output_poll_changed(dev);
}

static bool
nouveau_switcheroo_can_switch(const struct nvif_driver_func *driver)
{
	struct drm_device *dev = container_of(driver, struct nouveau_drm, driver)->dev;

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return atomic_read(&dev->open_count) == 0;
}

const struct nvif_driver_func_switcheroo
nouveau_switcheroo = {
	.set_state = nouveau_switcheroo_set_state,
	.reprobe = nouveau_switcheroo_reprobe,
	.can_switch = nouveau_switcheroo_can_switch,
};

void
nouveau_vga_lastclose(struct drm_device *dev)
{
	vga_switcheroo_process_delayed_switch();
}
