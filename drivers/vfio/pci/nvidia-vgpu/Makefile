# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(srctree)/$(src)/include

obj-$(CONFIG_NVIDIA_VGPU_MGR) += nvidia-vgpu-mgr.o
nvidia-vgpu-mgr-y := vgpu_mgr.o vgpu.o vgpu_types.o rpc.o

obj-$(CONFIG_NVIDIA_VGPU_VFIO_PCI) += nvidia-vgpu-vfio-pci.o
nvidia-vgpu-vfio-pci-y := vfio_main.o vfio_access.o
