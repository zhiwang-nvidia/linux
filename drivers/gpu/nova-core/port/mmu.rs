#![allow(dead_code)]
#![allow(unused)]

use kernel::prelude::*;

use core::fmt;
use core::fmt::Debug;
use kernel::sync::Arc;
use crate::gpu::GpuBase;
use crate::mmu::memory::NVKM_MM_PAGE_SHIFT;
use crate::mmu::memory::InstObj;
use crate::mmu::memory::InstMem;
use crate::mmu::memory::Memory;

pub(crate) const DMA_BITS: u8 = 47;

pub(crate) struct MmuPtC {
    _size: usize,
    _refs: usize,
}

impl MmuPtC {

    pub(crate) fn new(size: usize) -> Self {
        Self {
            _size: size,
            _refs: 0,
        }
    }

    pub(crate) fn get(instmem: Arc<InstMem>, size: usize, align: usize, zero: bool) -> Result<MmuPt> {
        if align < 0x1000 {


        }

        //let _ptc = Self::new(size);

        let pt = MmuPt::new(instmem, size, align, zero, false)?;
        Ok(pt)
    }

    pub(crate) fn put() {

    }
}

pub(crate) struct MmuPt {
    sub: bool,
    base: u16,
    pub(crate) addr: u64,
    pub(crate) memory: InstObj,
    //memory
    //list member
    //ptc/ptp ptrs?
}

impl MmuPt {
    pub(crate) fn new(instmem: Arc<InstMem>, size: usize, align: usize, zero: bool, sub: bool) -> Result<Self> {

        let memory = InstObj::new(instmem, size, align, zero, true)?;
        let addr = memory.addr()?;
        pr_info!("MmuPt: addr: {:#x}\n", addr);
        Ok(Self {
            sub,
            base : 0,
            addr,
            memory
        })
    }

    pub(crate) fn wrap(memory: InstObj) -> Result<Self> {
        let addr = memory.addr()?;
        Ok(Self {
            sub: false,
            base: 0,
            addr,
            memory,
        })
    }

    pub(crate) fn wo64(&mut self, offset: u64, val: u64) -> Result<()> {
        self.memory.wr64(self.base as u64 + offset, val)
    }

    pub(crate) fn fill64(&mut self, offset: u64, val: u64, count: usize) -> Result<()> {
        self.memory.fill64(self.base as u64 + offset, val, count)
    }

    pub(crate) fn wo128(&mut self, offset: u64, val1: u64, val2: u64) -> Result<()> {
        self.memory.wr64(self.base as u64 + offset, val1)?;
        self.memory.wr64(self.base as u64 + offset + 8, val2)
    }

    pub(crate) fn fill128(&mut self, offset: u64, val1: u64, val2: u64, count: usize) -> Result<()> {
        self.memory.fill128(self.base as u64 + offset, val1, val2, count)
    }
}

impl Drop for MmuPt {
    fn drop(&mut self) {
       pr_info!("Dropping MmuPt {:#x}", self.addr);
    }
}

impl Debug for MmuPt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {

       unsafe {
           let ptr = self as *const MmuPt as *mut MmuPt;
           (*ptr).memory.acquire();
           write!(f, "mmupt {:#x} ", self.addr)?;
           for i in 0..16 {
               write!(f, "{:#x} ", self.memory.rd32(self.base as u64 + i * 4).unwrap())?;
           }
           (*ptr).memory.release();
       }
       Ok(())
    }
}

pub(crate) const NVKM_MEM_VRAM: u8 = 0x1;
pub(crate) const NVKM_MEM_HOST: u8 = 0x2;
const NVKM_MEM_COMP: u8 = 0x4;
const NVKM_MEM_DISP: u8 = 0x8;
pub(crate) struct MmuHeap {
    heap_type: u8,
    size: u64,
}

const NVKM_MEM_KIND: u8 = 0x10;
const NVKM_MEM_MAPPABLE: u8 = 0x20;
pub(crate) const NVKM_MEM_COHERENT: u8 = 0x40;
pub(crate) const NVKM_MEM_UNCACHED: u8 = 0x80;

pub(crate) struct MmuType {
    pub mmu_type: u8,
    pub heap: u8,
}


pub(crate) struct Mmu {
    base: Arc<GpuBase>,
    pub dma_bits: u8,
    pub types: KVec<MmuType>,
    pub heaps: KVec<MmuHeap>,
    pub kindinfo: KindInfo,
}

pub(crate) struct KindInfo {
    pub kind: [u8; 16],
    pub invalid: u8,
}

impl KindInfo {
    fn new() -> Self {
        Self {
            kind: [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                   0x06, 0x06, 0x02, 0x01, 0x03, 0x04, 0x05, 0x07],
            invalid: 0x7,
        }
    }
}


impl Mmu {

    fn add_type(&mut self, heap: i32, mmu_type: u8) -> Result<()> {
        if heap >= 0 {
            self.types.push(MmuType {
                mmu_type: mmu_type | self.heaps[heap as usize].heap_type,
                heap: heap as u8,
            }, GFP_KERNEL)?;
        }
        Ok(())
    }

    fn add_heap(&mut self, heap_type: u8, size: u64) -> Result<i32> {
        if size != 0 {
            self.heaps.push(MmuHeap {
                heap_type,
                size,
            }, GFP_KERNEL)?;
            return Ok(self.heaps.len().wrapping_sub(1) as i32);
        }
        Ok(-1)
    }

    fn host(&mut self) -> Result<()> {
        let mut mmu_type: u8 = NVKM_MEM_KIND;

        /* Non-mappable system memory. */
        let heap = self.add_heap(NVKM_MEM_HOST, !0_u64)?;
        self.add_type(heap, mmu_type)?;

        /* Non-coherent, cached, system memory.
         *
         * Block-linear mappings of system memory must be done through
         * BAR1, and cannot be supported on systems where we're unable
         * to map BAR1 with write-combining.
         */
        mmu_type |= NVKM_MEM_MAPPABLE;
        self.add_type(heap, mmu_type)?;

        mmu_type |= NVKM_MEM_COHERENT;
        self.add_type(heap, mmu_type & !NVKM_MEM_KIND)?;

        /* Uncached system memory. */
        mmu_type |= NVKM_MEM_UNCACHED;
        self.add_type(heap, mmu_type)?;
        Ok(())
    }

    fn vram(&mut self, size_n: u64) -> Result<()> {

        let mut mmu_type: u8 = NVKM_MEM_KIND;
        let mut heap: u8 = NVKM_MEM_VRAM;

        heap |= NVKM_MEM_COMP;
        heap |= NVKM_MEM_DISP;

        let heap_n = self.add_heap(heap, size_n << NVKM_MM_PAGE_SHIFT)?;

        /* Add non-mappable VRAM types first so that they're preferred
         * over anything else.  Mixed-memory will be slower than other
         * heaps, it's prioritised last.
         */
        self.add_type(heap_n, mmu_type)?;

        self.host()?;

        if 1 == 1 {
            mmu_type |= NVKM_MEM_MAPPABLE;
            self.add_type(heap_n, mmu_type)?;
            mmu_type |= NVKM_MEM_COHERENT;
            mmu_type |= NVKM_MEM_UNCACHED;
            self.add_type(heap_n, mmu_type)?;
        }
        Ok(())
    }

    pub(crate) fn new(gpu_base: Arc<GpuBase>, heap_size: u64) -> Result<Self> {
        let mut mmu = Self {
            base: gpu_base,
            dma_bits: DMA_BITS,
            types: KVec::new(),
            heaps: KVec::new(),
            kindinfo: KindInfo::new(),
        };

        mmu.vram(heap_size)?;
        Ok(mmu)
    }
}
