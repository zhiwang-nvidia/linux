/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_VGA_H__
#define __NOUVEAU_VGA_H__

extern const struct nvif_driver_func_switcheroo nouveau_switcheroo;
void nouveau_vga_lastclose(struct drm_device *dev);

#endif
