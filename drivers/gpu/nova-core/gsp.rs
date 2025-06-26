// SPDX-License-Identifier: GPL-2.0

use kernel::device;
use kernel::dma::CoherentAllocation;
use kernel::pci;
use kernel::prelude::*;
use kernel::transmute::{AsBytes, FromBytes};
use kernel::{dma_write};

use crate::dma::DmaObject;
use crate::fb::FbLayout;
use crate::firmware::Firmware;
use crate::nvfw::r570_144 as fw;

pub(crate) const GSP_PAGE_SHIFT: usize = 12;
pub(crate) const GSP_PAGE_SIZE: usize = 1 << GSP_PAGE_SHIFT;
pub(crate) const GSP_HEAP_SHIFT: u64 = 1 << 20;

unsafe impl FromBytes for fw::GspFwWprMeta {}
unsafe impl AsBytes for fw::GspFwWprMeta {}
unsafe impl FromBytes for fw::GspSystemInfo {}
unsafe impl AsBytes for fw::GspSystemInfo {}

pub(crate) fn build_wpr_meta(
    dev: &device::Device<device::Bound>,
    fw: &Firmware,
    fb_layout: &FbLayout,
) -> Result<CoherentAllocation<fw::GspFwWprMeta>> {
    let wpr_meta =
        CoherentAllocation::<fw::GspFwWprMeta>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;
    dma_write!(wpr_meta[0].magic = fw::GSP_FW_WPR_META_MAGIC as u64)?;
    dma_write!(wpr_meta[0].revision = fw::GSP_FW_WPR_META_REVISION as u64)?;
    dma_write!(wpr_meta[0].sysmemAddrOfRadix3Elf = fw.gsp.lvl0_dma_handle() as u64)?;
    dma_write!(wpr_meta[0].sizeOfRadix3Elf = fw.gsp.size() as u64)?;
    dma_write!(wpr_meta[0].sysmemAddrOfBootloader = fw.bootloader.ucode.dma_handle())?;
    dma_write!(wpr_meta[0].sizeOfBootloader = fw.bootloader.ucode.size() as u64)?;
    dma_write!(wpr_meta[0].bootloaderCodeOffset = fw.bootloader.code_offset as u64)?;
    dma_write!(wpr_meta[0].bootloaderDataOffset = fw.bootloader.data_offset as u64)?;
    dma_write!(wpr_meta[0].bootloaderManifestOffset = fw.bootloader.manifest_offset as u64)?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_1
            .__bindgen_anon_1
            .sysmemAddrOfSignature = fw.gsp_sigs.dma_handle() as u64
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_1
            .__bindgen_anon_1
            .sizeOfSignature = fw.gsp_sigs.size() as u64
    )?;
    dma_write!(wpr_meta[0].gspFwRsvdStart = fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].nonWprHeapOffset = fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].nonWprHeapSize = fb_layout.heap.end - fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].gspFwWprStart = fb_layout.wpr2.start)?;
    dma_write!(wpr_meta[0].gspFwHeapOffset = fb_layout.wpr2_heap.start)?;
    dma_write!(wpr_meta[0].gspFwHeapSize = fb_layout.wpr2_heap.end - fb_layout.wpr2_heap.start)?;
    dma_write!(wpr_meta[0].gspFwOffset = fb_layout.elf.start)?;
    dma_write!(wpr_meta[0].bootBinOffset = fb_layout.boot.start)?;
    dma_write!(wpr_meta[0].frtsOffset = fb_layout.frts.start)?;
    dma_write!(wpr_meta[0].frtsSize = fb_layout.frts.end - fb_layout.frts.start)?;
    dma_write!(wpr_meta[0].gspFwWprEnd = fb_layout.vga_workspace.start & !(0x20000 - 1))?;
    dma_write!(wpr_meta[0].gspFwHeapVfPartitionCount = fb_layout.vf_partition_count)?;
    dma_write!(wpr_meta[0].fbSize = fb_layout.fb.end - fb_layout.fb.start)?;
    dma_write!(wpr_meta[0].vgaWorkspaceOffset = fb_layout.vga_workspace.start)?;
    dma_write!(
        wpr_meta[0].vgaWorkspaceSize = fb_layout.vga_workspace.end - fb_layout.vga_workspace.start
    )?;
    dma_write!(wpr_meta[0].bootCount = 0)?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcAddr = 0
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcRequestOffset = 0
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcReplyOffset = 0
    )?;
    dma_write!(wpr_meta[0].verified = 0)?;

    Ok(wpr_meta)
}

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

fn create_coherent_dma_object<A: AsBytes + FromBytes>(
    dev: &device::Device<device::Bound>,
    name: &'static str,
    libos: &mut DmaObject,
    libos_arg_nr: usize,
) -> Result<CoherentAllocation<A>> {
    let obj = CoherentAllocation::<A>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;

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
