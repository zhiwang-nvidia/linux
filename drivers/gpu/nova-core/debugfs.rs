use crate::gsp::GspMemObjects;
use core::ffi::c_void;
use core::ptr;
use kernel::prelude::*;
use kernel::{c_str, error::to_result};

extern "C" {
    fn nova_debugfs_create(name: *const i8) -> *mut c_void;
    fn nova_debugfs_destroy(debugfs: *mut c_void);
    fn nova_debugfs_create_log_files(
        debugfs: *mut c_void,
        loginit_info: *mut NovaLogBufferInfo,
        logintr_info: *mut NovaLogBufferInfo,
        logrm_info: *mut NovaLogBufferInfo,
    ) -> i32;
}

#[repr(C)]
struct NovaLogBufferInfo {
    name: *const i8,
    data: *mut c_void,
    size: usize,
}

pub(crate) struct NovaDebugfs {
    ptr: *mut c_void,
    loginit_info: KBox<NovaLogBufferInfo>,
    logintr_info: KBox<NovaLogBufferInfo>,
    logrm_info: KBox<NovaLogBufferInfo>,
}

impl NovaDebugfs {
    pub(crate) fn new(name: &str) -> Result<Self> {
        let name_cstr = kernel::str::CString::try_from_fmt(fmt!("{}", name))?;

        let ptr = unsafe { nova_debugfs_create(name_cstr.as_char_ptr() as *const i8) };

        if ptr.is_null() {
            return Err(ENOMEM);
        }

        let loginit_info = KBox::new(
            NovaLogBufferInfo {
                name: c_str!("LOGINIT").as_char_ptr() as *const i8,
                data: ptr::null_mut(),
                size: 0,
            },
            GFP_KERNEL,
        )?;

        let logintr_info = KBox::new(
            NovaLogBufferInfo {
                name: c_str!("LOGINTR").as_char_ptr() as *const i8,
                data: ptr::null_mut(),
                size: 0,
            },
            GFP_KERNEL,
        )?;

        let logrm_info = KBox::new(
            NovaLogBufferInfo {
                name: c_str!("LOGRM").as_char_ptr() as *const i8,
                data: ptr::null_mut(),
                size: 0,
            },
            GFP_KERNEL,
        )?;

        Ok(Self {
            ptr,
            loginit_info,
            logintr_info,
            logrm_info,
        })
    }

    pub(crate) fn create_log_files(&mut self, gsp_mem: &GspMemObjects<'_>) -> Result {
        self.loginit_info.data = gsp_mem.loginit.start_ptr() as *mut c_void;
        self.loginit_info.size = gsp_mem.loginit.size();

        self.logintr_info.data = gsp_mem.logintr.start_ptr() as *mut c_void;
        self.logintr_info.size = gsp_mem.logintr.size();

        self.logrm_info.data = gsp_mem.logrm.start_ptr() as *mut c_void;
        self.logrm_info.size = gsp_mem.logrm.size();

        let ret = unsafe {
            nova_debugfs_create_log_files(
                self.ptr,
                &mut *self.loginit_info as *mut _,
                &mut *self.logintr_info as *mut _,
                &mut *self.logrm_info as *mut _,
            )
        };

        to_result(ret)?;

        pr_info!("Nova debugfs: Created log files\n");
        Ok(())
    }
}

impl Drop for NovaDebugfs {
    fn drop(&mut self) {
        unsafe {
            nova_debugfs_destroy(self.ptr);
        }
    }
}

unsafe impl Send for NovaDebugfs {}
unsafe impl Sync for NovaDebugfs {}
