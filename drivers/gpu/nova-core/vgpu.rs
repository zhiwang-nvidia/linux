// SPDX-License-Identifier: GPL-2.0

#![allow(dead_code, unused)]

use core::alloc::Layout;
use core::ptr;
use core::ptr::NonNull;
use core::sync::atomic::{AtomicBool, Ordering};

use kernel::new_mutex;
use kernel::{bindings, device};
use kernel::alloc::Allocator;
use kernel::alloc::allocator::Kmalloc;
use kernel::pci;
use kernel::prelude::*;
use kernel::sync::{Arc, Mutex, UniqueArc};

use crate::driver::NovaCore;
use crate::gpu::Chipset;
use crate::gpu::Gpu;
use crate::gsp::rm_control::RmControlParams;
use crate::gsp::rm_control::RmControlMessageElement;
use crate::port::memory::VramObj;
use crate::port::vmm::Vma;
use crate::port::memory::NVKM_MM_PAGE_SHIFT;
use crate::port::memory::Memory;
use kernel::types::ForeignOwnable;

struct VGpuVfioHandleData {
    handle_data: bindings::nvidia_vgpu_vfio_handle_data,
}

#[repr(C)]
pub(crate) struct VGpuRmControlData {
    cmd: u32,
    size: usize,
    data: *mut core::ffi::c_void,
}

fn alloc_rm_control_data(
    cmd: u32,
    size: usize,
) -> Result<*mut core::ffi::c_void> {
    if size == 0 {
        return Err(EINVAL);
    }

    let total_size = core::mem::size_of::<VGpuRmControlData>() + size;
    let align = core::mem::align_of::<VGpuRmControlData>();

    let layout = Layout::from_size_align(total_size, align).map_err(|_| EINVAL)?;

    let allocation = Kmalloc::alloc(layout, GFP_KERNEL | __GFP_ZERO).map_err(|_| EINVAL)?;
    let raw_ptr = allocation.as_ptr();

    let control_ptr = raw_ptr as *mut VGpuRmControlData;
    let data_ptr = unsafe {
        (raw_ptr as *mut u8)
            .add(core::mem::size_of::<VGpuRmControlData>())
            as *mut core::ffi::c_void
    };

    unsafe {
        (*control_ptr).cmd = cmd;
        (*control_ptr).size = size;
        (*control_ptr).data = data_ptr;
    }

    Ok(data_ptr)
}

fn control_from_data_ptr(data_ptr: *mut c_void) -> *mut VGpuRmControlData {
    unsafe {
        (data_ptr as *mut u8)
            .sub(core::mem::size_of::<VGpuRmControlData>())
            as *mut VGpuRmControlData
    }
}

fn free_rm_control_data(data_ptr: *mut core::ffi::c_void) {
    if data_ptr.is_null() {
        return;
    }

    unsafe {
        let control_ptr = control_from_data_ptr(data_ptr);

        let size = (*control_ptr).size;
        let total_size = core::mem::size_of::<VGpuRmControlData>() + size;

        if let Ok(layout) = Layout::from_size_align(total_size, core::mem::align_of::<VGpuRmControlData>()) {
            let raw_ptr = control_ptr as *mut u8;
            let nonnull = NonNull::new_unchecked(raw_ptr);
            Kmalloc::free(nonnull, layout);
        }
    }
}

impl RmControlParams for VGpuRmControlData {
    fn to_bytes(&self) -> &[u8] {
        // SAFETY: `self.data` must point to a valid memory region of `self.size` bytes.
        unsafe { core::slice::from_raw_parts(self.data as *const u8, self.size as usize) }
    }
}

impl RmControlMessageElement for VGpuRmControlData {
    fn from_bytes(data: &[u8]) -> Result<Self> {
        if data.is_empty() {
            return Err(EINVAL);
        }

        let data_ptr = alloc_rm_control_data(0, data.len())?;

        // SAFETY: raw_ptr is valid and non-null, size is correct
        unsafe {
            ptr::copy_nonoverlapping(data.as_ptr(), data_ptr.cast::<u8>(), data.len());
        }

        Ok(VGpuRmControlData {
            cmd: 0,
            size: data.len(),
            data: data_ptr,
        })
    }
}

#[pin_data]
#[repr(C)]
pub(crate) struct VGPUMem {
    #[pin]
    base: bindings::nvidia_vgpu_mem,
    obj: VramObj,
    bar1_vma: Option<Arc<Vma>>,
    handle: *mut core::ffi::c_void,
}

#[pin_data]
pub(crate) struct VGpu {
    enabled: AtomicBool,
    #[pin]
    inner: Mutex<VGpuVfioHandleData>,
    vidmem_size: u64,
}

pub(crate) fn vgpu_is_supported(pdev: &pci::Device<device::Bound>, chipset: Chipset) -> bool {
    match pdev.sriov_get_totalvfs() {
        Ok(vfs) => vfs != 0 && chipset >= Chipset::AD102,
        Err(_) => false,
    }
}

impl VGpu {
    pub(crate) fn new(vgpu_enabled: bool, vidmem_size: u64) -> Result<Arc<Self>> {
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
            vidmem_size,
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

unsafe extern "C" fn rm_ctrl_get(client: *mut bindings::nvidia_vgpu_gsp_client, cmd: u32, size: u32) -> *mut core::ffi::c_void {
    pr_info!("rm ctrl get\n");

    let data_ptr = match alloc_rm_control_data(cmd, size as usize) {
        Ok(ptr) => ptr,
        Err(_) => return core::ptr::null_mut(),
    };

    data_ptr as *mut core::ffi::c_void
}

unsafe extern "C" fn rm_ctrl_wr(client: *mut bindings::nvidia_vgpu_gsp_client, ctrl: *mut core::ffi::c_void) -> i32 {
    pr_info!("rm ctrl wr\n");

    let handle = unsafe { (*client).gsp_client};
    let drv = handle as *mut NovaCore;
    let mut nova_core = unsafe { &mut *drv };
    let gpu = &mut nova_core.gpu;

    let control_ptr = control_from_data_ptr(ctrl) as *mut VGpuRmControlData;
    let control = unsafe { &mut *control_ptr };

    if let Some(msg) = gpu.rmcontrol.send::<VGpuRmControlData, VGpuRmControlData>(&gpu.bar, &mut gpu.cmdq, control.cmd, Some(control)).ok() {
        if !msg.data.is_null() {
            free_rm_control_data(msg.data);
        }
    }
    0
}

unsafe extern "C" fn rm_ctrl_rd(client: *mut bindings::nvidia_vgpu_gsp_client, cmd: u32, size: u32) -> *mut core::ffi::c_void {
    pr_info!("rm ctrl rd\n");

    let handle = unsafe { (*client).gsp_client};
    let drv = handle as *mut NovaCore;
    let mut nova_core = unsafe { &mut *drv };
    let gpu = &mut nova_core.gpu;

    let data_ptr = match alloc_rm_control_data(cmd, size as usize) {
        Ok(ptr) => ptr,
        Err(_) => return core::ptr::null_mut(),
    };

    let control_ptr = control_from_data_ptr(data_ptr);
    let control = unsafe { &mut *control_ptr };

    pr_info!("rm ctrl rd cmd {} size {} \n", cmd, size);

    if let Some(msg) = gpu.rmcontrol.send::<VGpuRmControlData, VGpuRmControlData>(&gpu.bar, &mut gpu.cmdq, cmd, Some(control)).ok() {
        free_rm_control_data(data_ptr);
        if msg.size == 0 {
            return core::ptr::null_mut();
        }
        return msg.data;
    }
    free_rm_control_data(data_ptr);
    core::ptr::null_mut()
}

unsafe extern "C" fn rm_ctrl_done(client: *mut bindings::nvidia_vgpu_gsp_client, ctrl: *mut core::ffi::c_void) {
    pr_info!("rm ctrl done {:?}\n", ctrl);

    free_rm_control_data(ctrl);
}

unsafe extern "C" fn alloc_chids(handle: *mut core::ffi::c_void, offset: *mut u32, count: u32) -> i32 {
    pr_info!("alloc chids\n");
    0
}

unsafe extern "C" fn free_chids(handle: *mut core::ffi::c_void, offset: u32, count: u32) {
    pr_info!("free chids\n");
}

unsafe extern "C" fn get_avail_chids(handle: *mut core::ffi::c_void) -> u32 {
    pr_info!("get avail chids\n");
    2048
}

unsafe extern "C" fn alloc_fbmem(handle: *mut core::ffi::c_void, info: *mut bindings::nvidia_vgpu_alloc_fbmem_info) -> *mut bindings::nvidia_vgpu_mem {
    pr_info!("alloc fbmem\n");

    let drv = handle as *mut NovaCore;
    let nova_core = unsafe { &*drv };
    let gpu = &nova_core.gpu;
    let info = unsafe { &*info };
    let mut shift: u32;

    if info.align != 0 {
        shift = info.align.ilog2();
    } else {
        shift = NVKM_MM_PAGE_SHIFT as u32;
    }

    let vramobj = VramObj::new(gpu.instmem.vram_mm.clone(), 0, 0x1, shift as u8, info.size as usize, true, true).unwrap();
    let fbmem: Pin<KBox<VGPUMem>> = KBox::new(
        VGPUMem {
            base: bindings::nvidia_vgpu_mem {
                addr: vramobj.addr().unwrap(),
                size: vramobj.size().unwrap(),
            },
            obj: vramobj,
            bar1_vma: None,
            handle: handle,
        }, GFP_KERNEL).unwrap().into();

    pr_info!("alloc fbmem {} {}\n", fbmem.base.addr, fbmem.base.size);
    fbmem.into_foreign() as _
}

unsafe extern "C" fn free_fbmem(mem: *mut bindings::nvidia_vgpu_mem) {
    pr_info!("free fbmem\n");

    let _fbmem : KBox<VGPUMem> = unsafe { KBox::from_foreign(mem as _) };
}

unsafe extern "C" fn get_total_fbmem_size(handle: *mut core::ffi::c_void) -> u64 {
    pr_info!("get total fbmem size\n");

    let vgpu = to_vgpu!(handle);

    vgpu.vidmem_size
}

const NOVA_VFIO_OPS: bindings::nvidia_vgpu_vfio_ops = bindings::nvidia_vgpu_vfio_ops {
    vgpu_is_enabled: Some(vgpu_is_enabled),
    attach_handle: Some(attach_handle),
    detach_handle: Some(detach_handle),
    alloc_gsp_client: Some(alloc_gsp_client),
    free_gsp_client: Some(free_gsp_client),
    get_gsp_client_handle: Some(get_gsp_client_handle),
    rm_ctrl_get: Some(rm_ctrl_get),
    rm_ctrl_wr: Some(rm_ctrl_wr),
    rm_ctrl_rd: Some(rm_ctrl_rd),
    rm_ctrl_done: Some(rm_ctrl_done),
    alloc_chids: Some(alloc_chids),
    free_chids: Some(free_chids),
    get_avail_chids: Some(get_avail_chids),
    alloc_fbmem: Some(alloc_fbmem),
    free_fbmem: Some(free_fbmem),
    get_total_fbmem_size: Some(get_total_fbmem_size),
};

#[no_mangle]
#[allow(unreachable_pub)]
pub unsafe extern "C" fn nova_vgpu_get_vfio_ops(
    _handle: *mut core::ffi::c_void,
) -> *const bindings::nvidia_vgpu_vfio_ops {
    pr_info!("get vfio ops\n");
    &NOVA_VFIO_OPS
}
