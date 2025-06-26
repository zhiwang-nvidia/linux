// SPDX-License-Identifier: GPL-2.0

use kernel::device;
use kernel::pci;
use kernel::prelude::*;

use crate::dma::DmaObject;
use crate::nvfw::r570_144 as fw;

pub(crate) const GSP_PAGE_SHIFT: usize = 12;
pub(crate) const GSP_PAGE_SIZE: usize = 1 << GSP_PAGE_SHIFT;
pub(crate) const GSP_HEAP_SHIFT: u64 = 1 << 20;

#[allow(unused)]
pub(crate) struct GspMemObjects {
    pub libos: DmaObject,
    pub loginit: DmaObject,
    pub logintr: DmaObject,
    pub logrm: DmaObject,
}

/// Generates the `ID8` identifier required for some GSP objects.
fn id8(name: &str) -> u64 {
    let mut bytes = [0u8; core::mem::size_of::<u64>()];

    for (c, b) in name.bytes().rev().zip(&mut bytes) {
        *b = c;
    }

    u64::from_ne_bytes(bytes)
}

/// Creates a self-mapping page table for `obj` at its beginning.
fn create_pte_array(obj: &mut DmaObject) {
    let num_pages = obj.size().div_ceil(GSP_PAGE_SIZE);
    let handle = obj.dma_handle();

    let ptes = unsafe {
        let ptr = obj
            .start_ptr_mut()
            .add(core::mem::size_of::<u64>())
            .cast::<u64>();
        core::slice::from_raw_parts_mut(ptr, num_pages)
    };

    for (i, pte) in ptes.iter_mut().enumerate() {
        *pte = handle as u64 + ((i as u64) << GSP_PAGE_SHIFT);
    }
}

/// Creates a new `DmaObject` with `name` of `size`, and register it into the `libos` object at
/// argument position `libos_arg_nr`.
fn create_dma_object(
    dev: &device::Device<device::Bound>,
    name: &'static str,
    size: usize,
    libos: &mut DmaObject,
    libos_arg_nr: usize,
) -> Result<DmaObject> {
    let mut obj = DmaObject::new(dev, size)?;
    create_pte_array(&mut obj);

    let arg_offset = libos_arg_nr * size_of::<fw::LibosMemoryRegionInitArgument>();
    let libos_start_ptr = unsafe { libos.start_ptr_mut().add(arg_offset) };

    let libos_mem_init_args = fw::LibosMemoryRegionInitArgument {
        id8: id8(name),
        pa: obj.dma_handle(),
        size: obj.size() as u64,
        kind: fw::LibosMemoryRegionKind_LIBOS_MEMORY_REGION_CONTIGUOUS as u8,
        loc: fw::LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_SYSMEM as u8,
    };
    unsafe {
        core::ptr::copy_nonoverlapping(
            &libos_mem_init_args as *const fw::LibosMemoryRegionInitArgument,
            libos_start_ptr as *mut fw::LibosMemoryRegionInitArgument,
            1,
        );
    };

    Ok(obj)
}

impl GspMemObjects {
    pub(crate) fn new(pdev: &pci::Device<device::Bound>) -> Result<Self> {
        let dev = pdev.as_ref();
        let mut libos = DmaObject::new(dev, GSP_PAGE_SIZE)?;
        let loginit = create_dma_object(dev, "LOGINIT", 0x10000, &mut libos, 0)?;
        let logintr = create_dma_object(dev, "LOGINTR", 0x10000, &mut libos, 1)?;
        let logrm = create_dma_object(dev, "LOGRM", 0x10000, &mut libos, 2)?;

        Ok(GspMemObjects {
            libos,
            loginit,
            logintr,
            logrm,
        })
    }
}
