// SPDX-License-Identifier: GPL-2.0

#![allow(dead_code, unused)]

use core::sync::atomic::{AtomicBool, Ordering};

use kernel::new_mutex;
use kernel::{bindings, device};
use kernel::pci;
use kernel::prelude::*;
use kernel::sync::{Arc, Mutex, UniqueArc};

use crate::driver::NovaCore;
use crate::gpu::Chipset;
use crate::gpu::Gpu;

#[pin_data]
pub(crate) struct VGpu {
    enabled: AtomicBool,
}

pub(crate) fn vgpu_is_supported(pdev: &pci::Device<device::Bound>, chipset: Chipset) -> bool {
    match pdev.sriov_get_totalvfs() {
        Ok(vfs) => vfs != 0 && chipset >= Chipset::AD102,
        Err(_) => false,
    }
}

impl VGpu {
    pub(crate) fn new(vgpu_enabled: bool) -> Result<Arc<Self>> {
        let vgpu = UniqueArc::pin_init(
            pin_init!(Self {
                enabled: AtomicBool::new(vgpu_enabled),
            }),
            GFP_KERNEL,
        )?;

        Ok(vgpu.into())
    }

    pub(crate) fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::SeqCst)
    }
}
