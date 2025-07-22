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

struct VGpuVfioHandleData {
    handle_data: bindings::nvidia_vgpu_vfio_handle_data,
}

#[pin_data]
pub(crate) struct VGpu {
    enabled: AtomicBool,
    #[pin]
    inner: Mutex<VGpuVfioHandleData>,
}

pub(crate) fn vgpu_is_supported(pdev: &pci::Device<device::Bound>, chipset: Chipset) -> bool {
    match pdev.sriov_get_totalvfs() {
        Ok(vfs) => vfs != 0 && chipset >= Chipset::AD102,
        Err(_) => false,
    }
}

impl VGpu {
    pub(crate) fn new(vgpu_enabled: bool) -> Result<Arc<Self>> {
        let vgpu = UniqueArc::pin_init(pin_init!(Self {
            enabled: AtomicBool::new(vgpu_enabled),
            inner <- new_mutex!(VGpuVfioHandleData {
                handle_data: bindings::nvidia_vgpu_vfio_handle_data {
                    vfio: bindings::nvidia_vgpu_vfio_handle_data__bindgen_ty_1 {
                        handle: core::ptr::null_mut(),
                        module: core::ptr::null_mut(),
                        private_data: core::ptr::null_mut(),
                        pf_event_notify_fn: None,
                        pf_detach_handle_fn: None,
                    },
                    pf: bindings::nvidia_vgpu_vfio_handle_data__bindgen_ty_2 {
                        driver_is_unbound: false,
                        driver_caps: [0; bindings::NVIDIA_VGPU_MAX_PF_DRIVER_CAPS as usize / 64],
                    },
                }
            }),
        }),
        GFP_KERNEL,
        )?;

        Ok(vgpu.into())
    }

    pub(crate) fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::SeqCst)
    }
}

macro_rules! to_vgpu {
    ($handle:expr) => {{
        let drv = $handle as *mut NovaCore;
        // SAFETY: `handle` is guaranteed by caller to be a valid NovaCore pointer
        let nova_core = unsafe { &*drv };
        &nova_core.gpu.vgpu
    }};
}

unsafe extern "C" fn vgpu_is_enabled(handle: *mut core::ffi::c_void) -> bool {
    let vgpu = to_vgpu!(handle);

    pr_info!("vgpu is enabled {} \n", vgpu.is_enabled());
    vgpu.is_enabled()
}

unsafe extern "C" fn attach_handle(
    handle: *mut core::ffi::c_void,
    attach_handle_data: *mut bindings::nvidia_vgpu_vfio_attach_handle_data,
) -> i32 {
    let vgpu = to_vgpu!(handle);
    let attach_data = unsafe { *attach_handle_data };
    let mut guard = vgpu.inner.lock();
    let mut ret = 0;

    pr_info!("attach handle\n");

    if !vgpu.is_enabled() {
        return ENODEV.to_errno();
    }

    let callback = match attach_data.pf_attach_handle_fn {
        Some(cb) => cb,
        None => return EINVAL.to_errno(),
    };

    ret = unsafe { callback(handle, &mut guard.handle_data as *mut _, attach_handle_data) };
    if ret != 0 {
        return ret;
    }

    if !unsafe { bindings::try_module_get(guard.handle_data.vfio.module) } {
        return EINVAL.to_errno();
    }

    ret
}

unsafe extern "C" fn detach_handle(handle: *mut core::ffi::c_void) {
    let vgpu = to_vgpu!(handle);
    let mut guard = vgpu.inner.lock();

    pr_info!("detach handle\n");

    if !vgpu.is_enabled() {
        return;
    }

    let callback = match guard.handle_data.vfio.pf_detach_handle_fn {
        Some(cb) => cb,
        None => return,
    };

    unsafe { callback(handle, &mut guard.handle_data as *mut _) };

    unsafe { bindings::module_put(guard.handle_data.vfio.module) };
}

unsafe extern "C" fn alloc_gsp_client(handle: *mut core::ffi::c_void, client: *mut bindings::nvidia_vgpu_gsp_client) -> i32 {
    let vgpu = to_vgpu!(handle);

    pr_info!("alloc gsp client\n");

    if !vgpu.is_enabled() {
        return ENODEV.to_errno();
    }

    unsafe { (*client).gsp_client = handle };
    0
}

unsafe extern "C" fn free_gsp_client(client: *mut bindings::nvidia_vgpu_gsp_client) {
    pr_info!("free gsp client\n");
}

unsafe extern "C" fn get_gsp_client_handle(client: *mut bindings::nvidia_vgpu_gsp_client) -> u32 {
    pr_info!("get gsp client handle\n");
    0
}

const NOVA_VFIO_OPS: bindings::nvidia_vgpu_vfio_ops = bindings::nvidia_vgpu_vfio_ops {
    vgpu_is_enabled: Some(vgpu_is_enabled),
    attach_handle: Some(attach_handle),
    detach_handle: Some(detach_handle),
    alloc_gsp_client: Some(alloc_gsp_client),
    free_gsp_client: Some(free_gsp_client),
    get_gsp_client_handle: Some(get_gsp_client_handle),
};

#[no_mangle]
#[allow(unreachable_pub)]
pub unsafe extern "C" fn nova_vgpu_get_vfio_ops(
    _handle: *mut core::ffi::c_void,
) -> *const bindings::nvidia_vgpu_vfio_ops {
    pr_info!("get vfio ops\n");
    &NOVA_VFIO_OPS
}
