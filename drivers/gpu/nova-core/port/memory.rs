#![allow(dead_code)]
#![allow(unused)]

use core::sync::atomic::{fence, Ordering};

use kernel::prelude::*;
use kernel::bindings;
use kernel::io::IoRaw;
use kernel::sync::Arc;
use kernel::sync::SpinLock;
use kernel::new_spinlock;
use kernel::page::{PAGE_SIZE, PAGE_SHIFT};
use crate::port::utils::order_base_2;
use crate::port::utils::GpuBase;
use crate::port::utils::align;
use crate::port::mmu::{NVKM_MEM_COHERENT, NVKM_MEM_UNCACHED};
use crate::port::mm::MemRangeNode;
use crate::port::mm::MemRange;
use crate::port::vmm::{Vmm,Vma,VmmMap,VmmInner,VmmStaticInfo,VmmPt};
use core::cell::UnsafeCell;
use crate::port::bar::Bar;

pub(crate) enum MemObjType {
    VRAM,
    DMA,
    SGL,
    INST,
}

#[derive(Clone,Copy,Debug)]
pub(crate) enum MemTarget {
    InstSRLost,
    Inst,
    Vram,
    Host,
    Ncoh,
}

pub(crate) const NVKM_MM_PAGE_SHIFT: usize = 12;

/// Trait representing memory operations
/// VramObj and InstanceObj implement these
pub(crate) trait Memory {
    fn obj_type(&self) -> MemObjType;
    fn size(&self) -> Result<u64>;
    fn addr(&self) -> Result<u64>;
    fn map(&mut self, offset: u64);
    fn kmap(&mut self, vmm: &Vmm, instmem: Arc<InstMem>) -> Result<()>;
    fn kmap_locked(&mut self, vmm_info: &VmmStaticInfo, vmm: &mut VmmInner, pd: &mut VmmPt) -> Result<()> {
        return Err(ENOSPC);
    }
    fn kunmap(&mut self);
    fn target(&self) -> MemTarget;
    fn page(&self) -> u8;
    fn rd32(&self, offset: u64) -> Result<u32>;
    fn wr32(&mut self, offset: u64, data: u32) -> Result<()>;
    fn wr64(&mut self, offset: u64, data: u64) -> Result<()>;
    fn fill64(&mut self, offset: u64, value: u64, count: usize) -> Result<()>;
    fn fill128(&mut self, offset: u64, value_lo: u64, value_hi: u64, count: usize) -> Result<()>;
}

/// Physical allocation in VRAM

pub(crate) struct VramNode {
    node: Arc<MemRangeNode>
}

impl VramNode {
    pub(crate) fn size(&self) -> u64 {
        (self.node.size() as u64) << NVKM_MM_PAGE_SHIFT
    }

    pub(crate) fn addr(&self) -> u64 {
        (self.node.addr() as u64) << NVKM_MM_PAGE_SHIFT
    }
}

pub(crate) struct VramObj {
    /* address in VRAM */
    pub nodes: KVec<VramNode>,
    mm: Option<Arc<MemRange>>,
    page: u8,
}

impl Memory for VramObj {
    fn obj_type(&self) -> MemObjType {
        MemObjType::VRAM
    }

    fn size(&self) -> Result<u64> {
        Ok(self.nodes[0].size())
    }

    fn addr(&self) -> Result<u64> {
        Ok(self.nodes[0].addr())
    }

    fn target(&self) -> MemTarget {
        MemTarget::Vram
    }

    fn page(&self) -> u8 {
        self.page as u8
    }

    fn map(&mut self, offset: u64) {

    }

    fn kmap(&mut self, vmm: &Vmm, instmem: Arc<InstMem>) -> Result<()> {
        Err(ENOSPC)
    }

    fn kunmap(&mut self) {
    }

    fn rd32(&self, offset: u64) -> Result<u32>{
        Err(ENOSPC)
    }
    fn wr32(&mut self, offset: u64, data: u32) -> Result<()> {
        Err(ENOSPC)
    }

    fn wr64(&mut self, offset: u64, data: u64) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill64(&mut self, offset: u64, value: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill128(&mut self, offset: u64, value_lo: u64, value_hi: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }
}

impl VramObj {

    pub(crate) fn vram_kmap(vramobj: VramObj, instmem: Arc<InstMem>) -> Result<InstObj> {
        InstObj::wrap(instmem, vramobj)
    }

    pub(crate) fn wrap(addr: usize, size: usize) -> Result<Self> {
        let mut nodes = KVec::with_capacity(1, GFP_KERNEL)?;
        nodes.push(VramNode {
            node: MemRangeNode::wrap(addr >> NVKM_MM_PAGE_SHIFT,
                                     size >> NVKM_MM_PAGE_SHIFT)?
        }, GFP_KERNEL);
        Ok(Self {
            nodes,
            mm: None,
            page: NVKM_MM_PAGE_SHIFT as u8,
        })

    }
    pub(crate) fn new(mm: Arc<MemRange>, heap: u8, mm_type: u8, rpage: u8, size: usize, contig: bool, back: bool) -> Result<Self> {
        let page: u8 = core::cmp::max(rpage, NVKM_MM_PAGE_SHIFT as u8);
        let szalign: usize = (1 << page) >> NVKM_MM_PAGE_SHIFT;
        let mut szmax: usize = align(size, 1 << page) >> NVKM_MM_PAGE_SHIFT;
        let szmin: usize;

        if contig {
            szmin = szmax;
        } else {
            szmin = szalign;
        }

        let mut nodes = KVec::with_capacity(1, GFP_KERNEL)?;

        loop {
            let node;

            if back {
                node = mm.tail(heap, mm_type, szmax, szmin, szalign)?;
            } else {
                node = mm.head(heap, mm_type, szmax, szmin, szalign)?;
            }

            szmax -= node.size();

            nodes.push(VramNode {
                node
            }, GFP_KERNEL);

            if szmax == 0 {
                break;
            }
        }
        let ret = Self {
            nodes,
            mm: Some(mm.clone()),
            page
        };

        pr_info!("allocated vram: {:#x} {:#x} {}\n", ret.addr()?, ret.size()?, ret.nodes.len());

        Ok(ret)
    }

    pub(crate) fn vram_map(&self, offset: u64, vmm: &Vmm, vma: Arc<Vma>, kind: u8) -> Result<()> {
        let mut vmmmap = VmmMap {
            memory: self,
            offset,
            kind,
            ro: 0,
            private: 0,
            vol: 0,
        };

        vmm.map(vma, &mut vmmmap)?;

        Ok(())
    }

    pub(crate) fn vram_map_locked(&mut self, offset: u64, pd: &mut VmmPt, sinfo: &VmmStaticInfo, vma: Arc<Vma>) -> Result<()> {
        let mut vmmmap = VmmMap {
            memory: self,
            offset,
            kind: 0,
            ro: 0,
            private: 0,
            vol: 0,
        };

        Vmm::map_internal(pd, sinfo, vma, &mut vmmmap)?;
        Ok(())
    }
}

impl Drop for VramObj {
    fn drop(&mut self) {

        match &self.mm {
            Some(mmp) => {
                for node in &self.nodes {
                    pr_info!("freeing vram {:#x} {:#x}\n", node.node.addr(), node.node.size());
                    mmp.free(node.node.clone());
                }
            }
            None => {}
        }
    }
}

/// Instance object, can be mapped into instance RAM (bar2)
/// Also can be written to via slow registers.
/// This is used to store page tables
/// Contains a vram allocation for physical storage.
pub(crate) struct InstObj {
    instmem: Arc<InstMem>,
    pub vram: VramObj,
    bar2_vma_addr: Option<u64>,
    bar2_io: Option<IoRaw::<PAGE_SIZE>>,
    bar2_map: Option<*mut core::ffi::c_void>,
    use_fast: Option<bool>,
    old_fast: bool,
}

impl Memory for InstObj {
    fn obj_type(&self) -> MemObjType {
        MemObjType::INST
    }
    fn size(&self) -> Result<u64> {
        self.vram.size()
    }

    fn addr(&self) -> Result<u64> {
        self.vram.addr()
    }

    fn target(&self) -> MemTarget {
        MemTarget::Vram
    }

    fn page(&self) -> u8 {
        self.vram.page()
    }

    fn map(&mut self, offset: u64) {
    }

    fn kmap(&mut self, vmm: &Vmm, instmem: Arc<InstMem>) -> Result<()> {
        // LOCKING RECURSION??
        let size = self.size()?;

        if self.bar2_map.is_some() {
            return Ok(());
        }

        loop {
            let bar = match vmm.get(false, true, false, 12, 0, size) {
                Err(ENOMEM) => { continue; }
                Err(x) => { return Err(x); }
                Ok(x) => { x }
            };

            self.vram.vram_map(0, vmm, bar.clone(), 0)?;

            self.bar2_vma_addr = Some(bar.addr());
            pr_info!("kmap inst {:#x} {:#x}\n", bar.clone().addr(), bar.clone().size());
            self.bar2_io = Some(unsafe { IoRaw::<PAGE_SIZE>::new((instmem.bar2_phys_base + bar.addr()) as usize, bar.size() as usize)? });
            self.bar2_map = Some(unsafe { self.bar2_io.as_ref().unwrap().remap(bar.size() as usize) });
            break;
        }
        Ok(())
    }

    fn kmap_locked(&mut self, vmm_info: &VmmStaticInfo, inner: &mut VmmInner, pd: &mut VmmPt) -> Result<()> {
        // LOCKING RECURSION??
        let size = self.size()?;

        if self.bar2_map.is_some() {
            return Ok(());
        }

        loop {
            let bar = match Vmm::get_internal(inner, pd, vmm_info, false, true, false, 12, 0, size) {
                Err(ENOMEM) => { continue; }
                Err(x) => { return Err(x); }
                Ok(x) => { x }
            };

            self.vram.vram_map_locked(0, pd, vmm_info, bar.clone())?;
            // map memory to bar
            // do ioremap
            self.bar2_vma_addr = Some(bar.addr());
            pr_info!("kmap inst {:#x} {:#x}\n", bar.clone().addr(), bar.clone().size());
            self.bar2_io = Some(unsafe { IoRaw::<PAGE_SIZE>::new((vmm_info.instmem.bar2_phys_base + bar.addr()) as usize, bar.size() as usize)? });
            self.bar2_map = Some(unsafe { self.bar2_io.as_ref().unwrap().remap(bar.size() as usize) });
            break;
        }
        Ok(())
    }

    fn kunmap(&mut self) {
        match self.instmem.get_bar().unwrap() {
            Some(bar) => {
                if self.bar2_vma_addr.is_some() {
                    pr_info!("kunmap inst {:#x}\n", self.bar2_vma_addr.unwrap());
                    bar.bar2_vmm().unwrap().put_addr(self.bar2_vma_addr.unwrap());
                }
            }
            _ => {}
        }

        if let Some(map) = self.bar2_map {
            unsafe { self.bar2_io.as_ref().unwrap().unmap(map) };
            self.bar2_map = None;
        }

        self.bar2_io = None;
    }

    fn rd32(&self, offset: u64) -> Result<u32> {
        match self.use_fast {
            Some(x) => {
                match x {
                    true => {
                        let data = unsafe { *((self.bar2_map.unwrap() as *mut u32).byte_offset(offset as isize)) };
                        pr_info!("rd32_fast: {:#x} {:#x}\n", self.vram.addr()? + offset, data);
                        Ok(data)
                    },
                    false => {
                        self.instmem.rd32_slow(self.vram.addr()? as u64, offset)
                    }
                }
            }
            None => {
                pr_err!("Illegal memory rd32 for unmapped");
                Err(EINVAL)
            }
        }
    }

    fn wr32(&mut self, offset: u64, data: u32) -> Result<()> {
        match self.use_fast {
            Some(x) => {
                match x {
                    true => {
                        unsafe { *((self.bar2_map.unwrap() as *mut u32).byte_offset(offset as isize)) = data };
                        Ok(())
                    }
                    false => {
                        // spinlock
                        self.instmem.wr32_slow(self.vram.addr()? as u64, offset, data)
                    }
                }
            }
            None => {
                pr_err!("Illegal memory rd32 for unmapped");
                Err(EINVAL)
            }
        }
    }

    fn wr64(&mut self, offset: u64, data: u64) -> Result<()> {
        self.wr32(offset, (data & 0xffffffff) as u32)?;
        self.wr32(offset + 4, (data >> 32) as u32)
    }

    fn fill64(&mut self, offset: u64, value: u64, count: usize) -> Result<()> {
        self.acquire()?;
        match self.bar2_map {
            Some(x) => {
                for i in 0..count {
                    self.wr64(offset + (i as u64) * 8, value)?;
                }
                self.release();
                Ok(())
            },
            None => {
                for i in 0..count {
                    self.wr64(offset + (i as u64) * 8, value)?;
                }
                self.release();
                Ok(())
            }
        }
    }

    fn fill128(&mut self, offset: u64, value_lo: u64, value_hi: u64, count: usize) -> Result<()> {
        self.acquire()?;
        match self.bar2_map {
            Some(x) => {
                for i in 0..count {
                    self.wr64(offset + (i as u64) * 16, value_lo)?;
                    self.wr64(offset + (i as u64) * 16 + 8, value_hi)?;
                }
                self.release();
                Ok(())
            },
            None => {
                for i in 0..count {
                    self.wr64(offset + (i as u64) * 16, value_lo)?;
                    self.wr64(offset + (i as u64) * 16 + 8, value_hi)?;
                }
                self.release();
                Ok(())
            }
        }
    }
}

impl InstObj {
    pub(crate) fn new(instmem: Arc<InstMem>, size: usize, align: usize, zero: bool, preserve: bool) -> Result<InstObj> {
        let page: u8 = core::cmp::max::<u32>(order_base_2(align), 12) as u8;
        pr_info!("instobj new {} {}\n", size, align);

        let mut obj = Self {
            instmem: instmem.clone(),
            vram: VramObj::new(instmem.vram_mm.clone(), 0, 1, page, size, true, true)?,
            bar2_vma_addr: None,
            bar2_map: None,
            bar2_io: None,
            use_fast: None,
            old_fast: false,
        };

        if zero {
            obj.fill64(0, 0, size >> 3)?;
        }
        Ok(obj)
    }

    pub(crate) fn wrap(instmem: Arc<InstMem>, vramobj: VramObj) -> Result<InstObj> {
        Ok(Self {
            instmem: instmem.clone(),
            vram: vramobj,
            bar2_vma_addr: None,
            bar2_map: None,
            bar2_io: None,
            use_fast: None,
            old_fast: false,
        })
    }

    pub(crate) fn raw_rd32(&self, offset: u64) -> Result<u32> {
        Ok(unsafe { *((self.bar2_map.unwrap() as *mut u32).byte_offset(offset as isize)) })
    }

    pub(crate) fn acquire(&mut self) -> Result<()> {
        match self.instmem.get_bar()? {
            None => { self.use_fast = Some(false); }
            Some(bar) => {
                if self.bar2_map.is_none() {
                    self.kmap(bar.bar2_vmm().unwrap(), self.instmem.clone())?;
                }
                self.use_fast = Some(self.bar2_map.is_some());
            }
        }

        Ok(())
    }

    pub(crate) fn set_slow(&mut self) {
        if self.use_fast.is_none() {
            return;
        }
        self.old_fast = self.use_fast.unwrap();
        self.use_fast = Some(false);
    }

    pub(crate) fn reset_slow(&mut self) {
        if self.use_fast.is_none() {
            return;
        }
        self.use_fast = Some(self.old_fast);
    }

    pub(crate) fn boot(&mut self, vmm_info: &VmmStaticInfo, inner: &mut VmmInner, pd: &mut VmmPt) -> Result<()> {
        self.kmap_locked(vmm_info, inner, pd)
    }

    pub(crate) fn release(&mut self) {
        //        self.kunmap();
        fence(Ordering::Acquire);
        match self.instmem.get_bar().unwrap() {
            None => {},
            Some(bar) => { let _ = bar.flush(); }
        }
        self.use_fast = None;
    }
}

impl Drop for InstObj {
    fn drop(&mut self) {
        self.kunmap();
    }
}

/// Management object for the bar2 instance memory.
/// Contains an LRU

struct InstBase {
    addr_base: u64,
}

#[pin_data]
#[repr(C)]
pub(crate) struct InstMem {
    pub base: Arc<GpuBase>,
    pub vram_mm: Arc<MemRange>,
    objs: KVec<InstObj>,
    bar: UnsafeCell<Option<Arc<Bar>>>,
    bar2_phys_base: u64,
    #[pin]
    addr_base: SpinLock<InstBase>,
}

impl InstMem {
    pub(crate) fn new(base: Arc<GpuBase>, vram_mm: Arc<MemRange>,
                      bar2_phys_base: u64) -> Result<Arc<Self>> {
        Arc::pin_init(pin_init!(Self {
            base,
            vram_mm,
            objs: KVec::new(),
            bar: UnsafeCell::new(None),
            bar2_phys_base,
            addr_base <- new_spinlock!(InstBase { addr_base: 0 }),
        }), GFP_KERNEL)
    }

    pub(crate) fn get_bar(&self) -> Result<Option<Arc<Bar>>> {
        Ok(unsafe { (*self.bar.get()).clone() })
    }

    pub(crate) fn set_bar(&self, bar: Arc<Bar>) -> Result<()> {
        unsafe { *self.bar.get() = Some(bar) };
        Ok(())
    }

    fn wr32_slow(&self, in_addr: u64, offset: u64, data: u32) -> Result<()> {
        let base = (in_addr + offset) & 0xffffff00000_u64;
        let addr = (in_addr + offset) & 0xfffff_u64;
        let bar = self.base.bar.try_access().ok_or(ENXIO)?;
        let mut guard = self.addr_base.lock();

        if guard.addr_base != base {
            bar.try_write32((base >> 16) as u32, 0x1700)?;
            guard.addr_base = base;
        }
        bar.try_write32(data, (0x700000 + addr) as usize)
    }

    fn rd32_slow(&self, in_addr: u64, offset: u64) -> Result<u32> {
        let base = (in_addr + offset) & 0xffffff00000_u64;
        let addr = (in_addr + offset) & 0xfffff_u64;
        let bar = self.base.bar.try_access().ok_or(ENXIO)?;
        let mut guard = self.addr_base.lock();
        if guard.addr_base != base {
            bar.try_write32((base >> 16) as u32, 0x1700)?;
            guard.addr_base = base;
        }
        bar.try_read32((0x700000 + addr) as usize)
    }
}

pub(crate) struct DmaMemObj {
    target: MemTarget,
    pages: u64,
    pub addr_array: *mut bindings::dma_addr_t,
}

impl Memory for DmaMemObj {
    fn obj_type(&self) -> MemObjType {
        MemObjType::DMA
    }

    fn size(&self) -> Result<u64> {
        Ok(self.pages << PAGE_SHIFT)
    }

    fn addr(&self) -> Result<u64> {
        Ok(!0_u64)
    }

    fn target(&self) -> MemTarget {
        self.target
    }

    fn page(&self) -> u8 {
        PAGE_SHIFT as u8
    }

    fn map(&mut self, offset: u64) {
    }
    fn kmap(&mut self, vmm: &Vmm, instmem: Arc<InstMem>) -> Result<()> {
        Err(ENOSPC)
    }

    fn kunmap(&mut self) {
    }

    fn rd32(&self, offset: u64) -> Result<u32>{
        Err(ENOSPC)
    }
    fn wr32(&mut self, offset: u64, data: u32) -> Result<()> {
        Err(ENOSPC)
    }

    fn wr64(&mut self, offset: u64, data: u64) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill64(&mut self, offset: u64, value: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill128(&mut self, offset: u64, value_lo: u64, value_hi: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }
}

impl DmaMemObj {
    pub(crate) fn new(addr: *mut bindings::dma_addr_t, heap: u8, mm_type: u8, rpage: u8, size: u64) -> Result<Self> {
        let target;

        if (mm_type & NVKM_MEM_COHERENT) != 0 && (mm_type & NVKM_MEM_UNCACHED) == 0 {
            target = MemTarget::Host;
        } else {
            target = MemTarget::Ncoh;
        }
        Ok(Self {
            target,
            pages: size >> PAGE_SHIFT,
            addr_array: addr,
        })
    }

}

pub(crate) struct SglMemObj {
    target: MemTarget,
    pages: u64,
    sgl: *mut bindings::scatterlist,
}

impl Memory for SglMemObj {
    fn obj_type(&self) -> MemObjType {
        MemObjType::SGL
    }
    fn size(&self) -> Result<u64> {
        Ok(self.pages << PAGE_SHIFT)
    }

    fn addr(&self) -> Result<u64> {
        Ok(!0_u64)
    }

    fn target(&self) -> MemTarget {
        self.target
    }

    fn page(&self) -> u8 {
        PAGE_SHIFT as u8
    }
    fn map(&mut self, offset: u64) {
    }
    fn kmap(&mut self, vmm: &Vmm, instmem: Arc<InstMem>) -> Result<()> {
        Err(ENOSPC)
    }

    fn kunmap(&mut self) {
    }

    fn rd32(&self, offset: u64) -> Result<u32>{
        Err(ENOSPC)
    }
    fn wr32(&mut self, offset: u64, data: u32) -> Result<()> {
        Err(ENOSPC)
    }

    fn wr64(&mut self, offset: u64, data: u64) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill64(&mut self, offset: u64, value: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }

    fn fill128(&mut self, offset: u64, value_lo: u64, value_hi: u64, count: usize) -> Result<()> {
        Err(ENOSPC)
    }
}

impl SglMemObj {
    pub(crate) fn new(sgl: *mut bindings::scatterlist, size: u64) -> Result<Self> {

        Ok(Self {
            target: MemTarget::Host,
            pages: size >> PAGE_SHIFT,
            sgl,
        })
    }

}
