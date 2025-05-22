.. SPDX-License-Identifier: GPL-2.0-only
.. include:: <isonum.txt>

=======================
NVIDIA vGPU VFIO Driver
=======================

:Copyright: |copy| 2025, NVIDIA CORPORATION. All rights reserved.
:Author: Zhi Wang <zhiw@nvidia.com>



Overview
========

NVIDIA vGPU[1] software enables powerful GPU performance for workloads
ranging from graphics-rich virtual workstations to data science and AI,
enabling IT to leverage the management and security benefits of
virtualization as well as the performance of NVIDIA GPUs required for
modern workloads. Installed on a physical GPU in a cloud or enterprise
data center server, NVIDIA vGPU software creates virtual GPUs that can
be shared across multiple virtual machines.

The vGPU architecture[2] can be illustrated as follow::

         +--------------------+    +--------------------+ +--------------------+ +--------------------+
         | Hypervisor         |    | Guest VM           | | Guest VM           | | Guest VM           |
         |                    |    | +----------------+ | | +----------------+ | | +----------------+ |
         | +----------------+ |    | |Applications... | | | |Applications... | | | |Applications... | |
         | |  NVIDIA        | |    | +----------------+ | | +----------------+ | | +----------------+ |
         | |  Virtual GPU   | |    | +----------------+ | | +----------------+ | | +----------------+ |
         | |  Manager       | |    | |  Guest Driver  | | | |  Guest Driver  | | | |  Guest Driver  | |
         | +------^---------+ |    | +----------------+ | | +----------------+ | | +----------------+ |
         |        |           |    +---------^----------+ +----------^---------+ +----------^---------+
         |        |           |              |                       |                      |
         |        |           +--------------+-----------------------+----------------------+---------+
         |        |                          |                       |                      |         |
         |        |                          |                       |                      |         |
         +--------+--------------------------+-----------------------+----------------------+---------+
        +---------v--------------------------+-----------------------+----------------------+----------+
        | NVIDIA                  +----------v---------+ +-----------v--------+ +-----------v--------+ |
        | Physical GPU            |   Virtual GPU      | |   Virtual GPU      | |   Virtual GPU      | |
        |                         +--------------------+ +--------------------+ +--------------------+ |
        +----------------------------------------------------------------------------------------------+

Each NVIDIA vGPU is analogous to a conventional GPU, having a fixed amount
of GPU framebuffer, and one or more virtual display outputs or "heads".
The vGPU’s framebuffer is allocated out of the physical GPU’s framebuffer
at the time the vGPU is created, and the vGPU retains exclusive use of
that framebuffer until it is destroyed.

The number of physical GPUs that a board has depends on the board. Each
physical GPU can support several different types of virtual GPU (vGPU).
vGPU types have a fixed amount of frame buffer, number of supported
display heads, and maximum resolutions. They are grouped into different
series according to the different classes of workload for which they are
optimized. Each series is identified by the last letter of the vGPU type
name.

NVIDIA vGPU supports Windows and Linux guest VM operating systems. The
supported vGPU types depend on the guest VM OS.

Architecture
============
::

                                    +--------------------+ +--------------------+ +--------------------+
                                    | Linux VM           | | Windows VM         | | Guest VM           |
                                    | +----------------+ | | +----------------+ | | +----------------+ |
                                    | |Applications... | | | |Applications... | | | |Applications... | |
                                    | +----------------+ | | +----------------+ | | +----------------+ | ...
                                    | +----------------+ | | +----------------+ | | +----------------+ |
                                    | |  Guest Driver  | | | |  Guest Driver  | | | |  Guest Driver  | |
                                    | +----------------+ | | +----------------+ | | +----------------+ |
                                    +---------^----------+ +----------^---------+ +----------^---------+
                                              |                       |                      |
                                   +--------------------------------------------------------------------+
                                   |+--------------------+ +--------------------+ +--------------------+|
                                   ||       QEMU         | |       QEMU         | |       QEMU         ||
                                   ||                    | |                    | |                    ||
                                   |+--------------------+ +--------------------+ +--------------------+|
                                   +--------------------------------------------------------------------+
                                              |                       |                      |
        +-----------------------------------------------------------------------------------------------+
        |                           +----------------------------------------------------------------+  |
        |                           |                                VFIO                            |  |
        |                           |                                                                |  |
        | +-----------------------+ | +-------------------------------------------------------------+|  |
        | |                       | | |                                                             ||  |
        | |     nova_core        <--->|                                                             ||  |
        | +    (core driver)      | | |                  NVIDIA vGPU VFIO Driver                    ||  |
        | |                       | | |                                                             ||  |
        | |                       | | +-------------------------------------------------------------+|  |
        | +--------^--------------+ +----------------------------------------------------------------+  |
        |          |                          |                       |                      |          |
        +-----------------------------------------------------------------------------------------------+
                   |                          |                       |                      |
        +----------|--------------------------|-----------------------|----------------------|----------+
        |          v               +----------v---------+ +-----------v--------+ +-----------v--------+ |
        |  NVIDIA                  |       PCI VF       | |       PCI VF       | |       PCI VF       | |
        |  Physical GPU            |                    | |                    | |                    | |
        |                          |   (Virtual GPU)    | |   (Virtual GPU)    | |    (Virtual GPU)   | |
        |                          +--------------------+ +--------------------+ +--------------------+ |
        +-----------------------------------------------------------------------------------------------+

Each virtual GPU (vGPU) instance is implemented atop a PCIe Virtual
Function (VF). The NVIDIA vGPU VFIO driver, in coordination with the
VFIO framework, operates directly on these VFs to enable key
functionalities including vGPU type selection, dynamic instantiation and
destruction of vGPU instances, support for live migration, and warm
update...

At the low level, the NVIDIA vGPU VFIO driver interfaces with a core
driver aka nova_core, which provides the necessary abstractions and
mechanisms to access and manipulate the underlying GPU hardware resources.

Core Driver
===========

The primary deployment model for cloud service providers (CSPs) and
enterprise environments is to have a standalone, minimal driver stack
with the vGPU support and other essential components. Thus, a minimal
core driver is required to support the NVIDIA vGPU VFIO driver.

Requirements To A Core Driver
=============================

The NVIDIA vGPU VFIO driver searches the supported core drivers by driver
names when loading. Once a supported core driver is found, the VFIO driver
generates a core driver handle for the following interactions with the core
driver.

With the handle, the VFIO driver first check if the vGPU support is
enabled in the core driver.

The core driver returns vGPU is supported on this PF if:

- The device advertise SRIOV caps.
- The device is in the supported device list in the core driver.
- The GSP microcode loaded by the core driver supports vGPU. Some core
  drivers, e.g. NVKM, can support multiple version of GSP microcode.
- The required initialization for vGPU support succeeds.

The core driver handle data is per-PF and shared among VFs. It contains the
two parts: the core driver part and the VFIO driver part. The core driver
part contains core driver status, capabilities for the VFIO driver to
validate. The VFIO driver part contains the data registered to the core
driver. E.g. event handlers, private data.

If the VFIO driver hasn't been attached with the core driver, the VFIO
driver attaches the handle data with the core driver. The core driver
functions are available to the VFIO driver after the attachment.

The core driver is responsible for the locking to protect the handle
data in attachment/detachment as it can be accessed in multiple paths
of VFIO driver probing/remove.

Beside the core driver attachment and handle management, the core driver
is required to provide the following functions to support the VFIO driver:

Enumeration:

- The total FB memory size of the current GPU.
- The available channel amount.
- The complete engine bitmap.

GSP RPC manipulation:

- Allocate/de-allocate a GSP client.
- Get the handle of a GSP client.
- Allocate/de-allocate RM control.
- Issue RM controls.

Channel ID Management:

The NVIDIA vGPU VFIO driver expects the core driver manages a reserved
channel pool that is only meant for vGPU. Other coponents in the core
driver should have the knowledge about the channel ID from the reserved
pool is for vGPUs. E.g. reporting channel fault and events to the VFIO
driver. It requires the following functions:

- Allocate channel IDs from the reserved pool.
- Free the channel IDs.

FB Memory Management:

- Allocate the FB memory by an allocation info.
  The allocation info contains the requirements from the VFIO driver
  besides size. E.g. fixed offset allocation, alignment requirements.
- Free the FB memory.

FB Memory Mapping:

- Map the FB memory to BAR1 and a channel VMM by a mapping info
  The mapping info contains the requirements from the VFIO driver. E.g.
  start offset to map the allocated FB memory, map size, huge page,
  special memory kind.
- Unmap the FB memory.

CE Workload Submission:

- CE channel allocation/deallocation.
- Pushbuf manipulation.

Event forwarding:
- Nonstall event.
- SRIOV configuration event.
- Core driver unbinding event.

vGPU types
==========

Each type of vGPU is designed to meet specific requirements, from
supporting multiple users with demanding graphics applications to
powering AI workloads in virtualized environments.

To create a vGPU associated with a vGPU type, the vGPU type blobs are
required to be uploaded to GSP firmware. A vGPU metadata file is
introduced to host the vGPU type blobs and will be loaded by the VFIO
driver from the userspace when loading.

The vGPU metafile can be found at::

        https://github.com/zhiwang-nvidia/vgpu-tools/tree/metadata


Create vGPUs
============

The VFs can be enabled via (for example 2 VFs)::

        echo 2 > /sys/bus/pci/devices/0000\:c1\:00.0/sriov_numvfs

After the VFIO driver is loaded. A sysfs interface is exposed to select
the vGPU types::

        cat /sys/devices/pci0000:3a/0000:3a:00.0/0000:3b:00.0/0000:3c:10.0/0000:3e:00.5/nvidia/creatable_vgpu_types
        ID    : vGPU Name
        941   : NVIDIA RTX6000-Ada-1Q
        942   : NVIDIA RTX6000-Ada-2Q
        943   : NVIDIA RTX6000-Ada-3Q
        944   : NVIDIA RTX6000-Ada-4Q
        945   : NVIDIA RTX6000-Ada-6Q
        946   : NVIDIA RTX6000-Ada-8Q
        947   : NVIDIA RTX6000-Ada-12Q
        948   : NVIDIA RTX6000-Ada-16Q
        949   : NVIDIA RTX6000-Ada-24Q
        950   : NVIDIA RTX6000-Ada-48Q

A valid vGPU type must be chosen for the VF before using the VFIO device::

        $ echo 941 > /sys/bus/pci/devices/0000\:c1\:00.4/nvidia/current_vgpu_type

To de-select the vGPU type::

        $ echo 0 > /sys/bus/pci/devices/0000\:c1\:00.4/nvidia/current_vgpu_type

Once the vGPU is select, the VFIO device is ready to be used by QEMU. The
VFIO device must be closed before the user can de-select the vGPU type.

References
==========

1. See Documentation/driver-api/vfio.rst for more information on VFIO.
