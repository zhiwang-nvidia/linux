#![allow(non_upper_case_globals)]
#![allow(dead_code)]
#![allow(unused)]

use kernel::str::{CStr,CString};
use kernel::c_str;
use core::fmt::Debug;
use core::fmt;
use core::cell::UnsafeCell;
use crate::{align64, is_aligned};
use kernel::prelude::*;
use kernel::bindings;
use kernel::page::{PAGE_SIZE, PAGE_SHIFT, PAGE_MASK};
use kernel::new_mutex;
use kernel::rbtree::RBTree;
use kernel::sync::{Arc, Mutex, UniqueArc, LockClassKey};
use kernel::static_lock_class;
use kernel::list::{
    List, ListArc, ListLinks,
};

#[doc(hidden)]
#[macro_export]
macro_rules! runtime_optional_name {
    ($name:expr) => {
        match $name {
            None => c_str!(::core::concat!(::core::file!(), ":", ::core::line!())),
            Some(x) => x,
        }
    };
}

use crate::gpu::GpuBase;
use crate::mmu::memory::{VramNode, VramObj, InstObj, MemObjType, DmaMemObj, SglMemObj};
use crate::mmu::memory::{Memory, InstMem, MemTarget};

use crate::mmu::mmu::{MmuPt, MmuPtC};
use crate::{timer_nsec, timer_msec};
use crate::timer::TimerWait;

const NVKM_VMM_PAGE_SPARSE: u8 = 0x01;
const NVKM_VMM_PAGE_VRAM: u8 = 0x02;
const NVKM_VMM_PAGE_HOST: u8 = 0x04;
const NVKM_VMM_PAGE_COMP: u8 = 0x08;

pub(crate) const NVKM_VMM_TYPE_UNMANAGED: u8 = 1;
const NVKM_VMM_TYPE_MANAGED: u8 = 2;
const NVKM_VMM_TYPE_RAW: u8 = 3;

const NVKM_VMM_PAGE_Sxxx: u8 = NVKM_VMM_PAGE_SPARSE;
const NVKM_VMM_PAGE_xVxx: u8 = NVKM_VMM_PAGE_VRAM;
const NVKM_VMM_PAGE_SVxx: u8 = NVKM_VMM_PAGE_Sxxx | NVKM_VMM_PAGE_VRAM;
const NVKM_VMM_PAGE_xxHx: u8 = NVKM_VMM_PAGE_HOST;
const NVKM_VMM_PAGE_SxHx: u8 = NVKM_VMM_PAGE_Sxxx | NVKM_VMM_PAGE_HOST;
const NVKM_VMM_PAGE_xVHx: u8 = NVKM_VMM_PAGE_xVxx | NVKM_VMM_PAGE_HOST;
const NVKM_VMM_PAGE_SVHx: u8 = NVKM_VMM_PAGE_SVxx | NVKM_VMM_PAGE_HOST;
const NVKM_VMM_PAGE_xVxC: u8 = NVKM_VMM_PAGE_xVxx | NVKM_VMM_PAGE_COMP;
const NVKM_VMM_PAGE_SVxC: u8 = NVKM_VMM_PAGE_SVxx | NVKM_VMM_PAGE_COMP;
const NVKM_VMM_PAGE_xxHC: u8 = NVKM_VMM_PAGE_xxHx | NVKM_VMM_PAGE_COMP;
const NVKM_VMM_PAGE_SxHC: u8 = NVKM_VMM_PAGE_SxHx | NVKM_VMM_PAGE_COMP;

const NVKM_VMM_PFN_ADDR: u64 = 0xfffffffffffff000_u64;
const NVKM_VMM_PFN_ADDR_SHIFT: u32 = 12;
const NVKM_VMM_PFN_APER: u64 = 0x0000000000000f00_u64;
const NVKM_VMM_PFN_HOST: u64 = 0x0000000000000000_u64;
const NVKM_VMM_PFN_VRAM: u64 = 0x0000000000000010_u64;
const NVKM_VMM_PFN_A:    u64 = 0x0000000000000004_u64;
const NVKM_VMM_PFN_W:    u64 = 0x0000000000000002_u64;
const NVKM_VMM_PFN_V:    u64 = 0x0000000000000001_u64;
const NVKM_VMM_PFN_NONE: u64 = 0x0000000000000000_u64;

const NVKM_VMM_PTE_SPARSE: u8 = 0x80;
const NVKM_VMM_PTE_VALID: u8 = 0x40;
const NVKM_VMM_PTE_SPTES: u8 = 0x3f;

const VMM_TRACE: bool = false;

pub(crate) struct VmmPt {
    pt: [Option<MmuPt>; 2],
    refs: [u32; 2],
    page: u8,
    sparse: bool,
    pde: KVVec<(bool, Option<VmmPt>)>,
    pte: KVec<u8>,
}

impl VmmPt {
    pub(crate) fn new(desc: &VmmDescType, sparse: bool, lvl: usize, page: Option<&VmmPage>) -> Result<Self> {
        let pten = 1_u32.wrapping_shl(desc.bits() as u32);

        pr_info!("new {} {}\n", pten, desc.bits() );
        let lpte = match desc {
            VmmDescType::Spt(_) => {
                let pair = VmmPage::find_pair_desc(page.unwrap());
                pr_info!("Setting up SPT {} {}\n", desc.bits(), pair.bits());
                pten >> (desc.bits() - pair.bits())
            },
            VmmDescType::Lpt(_) => {
                pr_info!("Setting up LPT {}\n", pten);
                pten
            },
            _ => { 0 }
        };

        let pg = match page {
            None => { 0 },
            Some(x) => { x.shift }
        };

        let pde = match desc {
            VmmDescType::PgdPd0(_) => { pten },
            VmmDescType::PgdPd1(_) => { pten },
            _ => { 0 }
        };

        let mut pte_v = KVec::with_capacity(lpte as usize, GFP_KERNEL)?;
        for _i in 0..lpte {
            pte_v.push(0, GFP_KERNEL)?;
        }

        let mut pde_v = KVVec::with_capacity(pde as usize, GFP_KERNEL)?;
        for _i in 0..pde {
            pde_v.push((false, None), GFP_KERNEL)?;
        }

        Ok(Self {
            sparse: sparse,
            pte: pte_v,
            pde: pde_v,
            page: pg,
            pt: [None, None],
            refs: [0, 0],
        })
    }
}

impl Debug for VmmPt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "page: {} pt {:?} {:?}", self.page, self.pt[0], self.pt[1])?;
        writeln!(f, "pte: {:x?}", self.pte)?;
        for i in 0..self.pde.len() {
            if self.pde[i].1.is_some() {
                writeln!(f, "pde: {} {:?}", i, self.pde[i].1.as_ref().unwrap())?;
            }
        }
        Ok(())
    }
}

impl Drop for VmmPt {
    fn drop(&mut self) {
        pr_info!("Dropping VmmPt");
    }
}

pub(crate) trait VmmDescFunc {
    fn invalid(_pt: &mut MmuPt, _ptei: u32, _ptes: u32) -> Result<()> {
        Err(EINVAL)
    }

    fn unmap(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        Err(EINVAL)
    }

    fn sparse(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        Err(EINVAL)
    }

    fn pde(pt: &mut VmmPt, pdei: u32) -> Result<()> {
        Err(EINVAL)
    }

    fn mem(pt: &mut MmuPt, ptei: u32, ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        Err(EINVAL)
    }

    fn dma(pt: &mut MmuPt, ptei: u32, ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        Err(EINVAL)
    }

    fn sgl(pt: &mut MmuPt, ptei: u32, ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        Err(EINVAL)
    }

    fn pfn(&mut self, pt: &MmuPt, ptei: u32, ptes: u32) {}
    fn pfn_clear(&mut self, pt: &MmuPt, ptei: u32, ptes: u32) -> Result<bool> { Err(EINVAL) }
    fn pfn_unmap(&mut self, pt: &MmuPt, ptei: u32, ptes: u32) {}
}

pub(crate) enum VmmDescType {
    Empty(VmmDescNone),
    PgdPd0(VmmDescPd0),
    PgdPd1(VmmDescPd1),
    Spt(VmmDescSPT),
    Lpt(VmmDescLPT),
}

impl VmmDescType {
    fn tname(&self) -> &'static str {
        match self {
            VmmDescType::Empty(_) => { "Empty" },
            VmmDescType::PgdPd0(x) => { "PGD0" }
            VmmDescType::PgdPd1(x) => { "PGD1" }
            VmmDescType::Spt(x) => { "SPT" }
            VmmDescType::Lpt(x) => { "LPT" }
        }
    }

    fn bits(&self) -> u8 {
        match self {
            VmmDescType::Empty(_) => { 0 },
            VmmDescType::PgdPd0(x) => { x.base.bits }
            VmmDescType::PgdPd1(x) => { x.base.bits }
            VmmDescType::Spt(x) => { x.base.bits }
            VmmDescType::Lpt(x) => { x.base.bits }
        }
    }

    fn size(&self) -> u8 {
        match self {
            VmmDescType::Empty(_) => { 0 },
            VmmDescType::PgdPd0(x) => { x.base.size }
            VmmDescType::PgdPd1(x) => { x.base.size }
            VmmDescType::Spt(x) => { x.base.size }
            VmmDescType::Lpt(x) => { x.base.size }
        }
    }

    fn align(&self) -> u32 {
        match self {
            VmmDescType::Empty(_) => { 0 },
            VmmDescType::PgdPd0(x) => { x.base.align }
            VmmDescType::PgdPd1(x) => { x.base.align }
            VmmDescType::Spt(x) => { x.base.align }
            VmmDescType::Lpt(x) => { x.base.align }
        }
    }

    fn has_invalid(&self) -> bool {
        match self {
            VmmDescType::Lpt(x) => { true }
            _ => { false }
        }
    }

    fn invalid(&self, pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        match self {
            VmmDescType::Lpt(x) => {
                VmmDescLPT::invalid(pt, ptei, ptes)
            }
            _ => { Ok(()) }
        }
    }

    fn unmap(&self, pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        match self {
            VmmDescType::Spt(x) => {
                VmmDescSPT::unmap(pt, ptei, ptes)
            }
            VmmDescType::Lpt(x) => {
                VmmDescLPT::unmap(pt, ptei, ptes)
            }
            VmmDescType::PgdPd0(x) => {
                VmmDescPd0::unmap(pt, ptei, ptes)
            }
            VmmDescType::PgdPd1(x) => {
                VmmDescPd1::unmap(pt, ptei, ptes)
            }
            _ => { Ok(()) }
        }
    }

    fn sparse(&self, pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        match self {
            VmmDescType::Spt(x) => {
                VmmDescSPT::sparse(pt, ptei, ptes)
            }
            VmmDescType::Lpt(x) => {
                VmmDescLPT::sparse(pt, ptei, ptes)
            }
            VmmDescType::PgdPd0(x) => {
                VmmDescPd0::sparse(pt, ptei, ptes)
            }
            VmmDescType::PgdPd1(x) => {
                VmmDescPd1::sparse(pt, ptei, ptes)
            }
            _ => { Ok(()) }
        }
    }

    fn pde(&self, pgd: &mut VmmPt, pdei: u32) -> Result<()> {
        match self {
            VmmDescType::PgdPd0(x) => {
                VmmDescPd0::pde(pgd, pdei)
            }
            VmmDescType::PgdPd1(x) => {
                VmmDescPd1::pde(pgd, pdei)
            }
            _ => { Ok(()) }
        }
    }

    fn invalidfn(&self) -> Option<ClrFn> {
        match self {
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::invalid) }
            _ => { None }
        }
    }

    fn sparsefn(&self) -> Option<ClrFn> {
        match self {
            VmmDescType::Empty(_) => { None },
            VmmDescType::PgdPd0(x) => { Some(VmmDescPd0::sparse) }
            VmmDescType::PgdPd1(x) => { Some(VmmDescPd1::sparse) }
            VmmDescType::Spt(x) => { Some(VmmDescSPT::sparse) }
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::sparse) }
            _ => { None }
        }
    }

    fn unmapfn(&self) -> Option<ClrFn> {
        match self {
            VmmDescType::Empty(_) => { None },
            VmmDescType::PgdPd0(x) => { Some(VmmDescPd0::unmap) }
            VmmDescType::PgdPd1(x) => { Some(VmmDescPd1::unmap) }
            VmmDescType::Spt(x) => { Some(VmmDescSPT::unmap) }
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::unmap) }
            _ => { None }
        }
    }

    fn memfn(&self) -> Option<MapFn> {
        match self {
            VmmDescType::Empty(_) => { None },
            VmmDescType::PgdPd0(x) => { Some(VmmDescPd0::mem) }
            VmmDescType::PgdPd1(x) => { Some(VmmDescPd1::mem) }
            VmmDescType::Spt(x) => { Some(VmmDescSPT::mem) }
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::mem) }
        }
    }

    fn dmafn(&self) -> Option<MapFn> {
        match self {
            VmmDescType::Empty(_) => { None },
            VmmDescType::PgdPd0(x) => { Some(VmmDescPd0::dma) }
            VmmDescType::PgdPd1(x) => { Some(VmmDescPd1::dma) }
            VmmDescType::Spt(x) => { Some(VmmDescSPT::dma) }
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::dma) }
        }
    }

    fn sglfn(&self) -> Option<MapFn> {
        match self {
            VmmDescType::Empty(_) => { None },
            VmmDescType::PgdPd0(x) => { Some(VmmDescPd0::sgl) }
            VmmDescType::PgdPd1(x) => { Some(VmmDescPd1::sgl) }
            VmmDescType::Spt(x) => { Some(VmmDescSPT::sgl) }
            VmmDescType::Lpt(x) => { Some(VmmDescLPT::sgl) }
        }
    }
}

/// macro to shift a 64-bit bit field
#[macro_export]
macro_rules! bit_u64 {
    ($bit: expr) => {
        1_u64 << $bit
    }
}

pub(crate) struct VmmDesc {
    bits: u8,
    size: u8,
    align: u32,
}

impl VmmDesc {

    fn pde(pt: &MmuPt, in_data: u64) -> u64 {

        let mut out_data = in_data;

        match pt.memory.target() {
            MemTarget::Vram => { out_data |= 1_u64 << 1; }
            MemTarget::Host => { out_data |= 2_u64 << 1;
                                 out_data |= bit_u64!(3); // VOL
            }
            MemTarget::Ncoh => { out_data |= 3_u64 << 1; }
            (x) => { pr_err!("Unknown mem target {:?}\n", x); return 0; }
        }

        out_data |= pt.addr >> 4;

        if VMM_TRACE {
            pr_info!("pde: {:#x} {:#x}\n", pt.addr, out_data);
        }

        // target
        //
        out_data
    }

    fn pgt_unmap(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        pt.fill64(ptei as u64 * 8, 0_u64, ptes as usize)
    }

    fn pgt_sparse(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        /* VALID_FALSE + VOL tells the MMU to treat the PTE as sparse. */
        pt.fill64(ptei as u64 * 8, bit_u64!(3) /* VOL */, ptes as usize)
    }
}

pub(crate) struct VmmDescNone {
    base: VmmDesc,
}

impl VmmDescFunc for VmmDescNone {
}

pub(crate) struct VmmDescSPT {
    base: VmmDesc,
}

impl VmmDescSPT {
    fn pte(pt: &mut MmuPt, in_ptei: u32, in_ptes: u32,
           addr: u64, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        let mut data = (addr >> 4) | map_internal.map_type;
        let mut ptei = in_ptei;
        let mut ptes = in_ptes;

        map_internal.map_type += (ptes * map_internal.ctag as u32) as u64;
        if VMM_TRACE {
            pr_info!("pte {:#x} {:#x} {:#x} {:#x} {:#x}\n", data, map_internal.map_type, map_internal.next, ptei, ptes);
        }
        while ptes != 0 {
            pt.wo64((ptei * 8) as u64, data)?;
            ptei += 1;
            data += map_internal.next;
            ptes -= 1;
        }
        Ok(())
    }
}

/// Internel iterator for iterating each PTE write to the page table
#[macro_export]
macro_rules! vmm_map_iter {
    ($pt: expr, $mptes: expr, $mptei: expr, $map_internal: expr,
     $size: expr, $base: expr, $fill: expr, $next: expr) => {
        $pt.memory.acquire()?;
        let _shift = $map_internal.pg_shift;
        while $mptes != 0 {
            let mut _ptes: u32 = (($size - $map_internal.off) >> _shift) as u32;
            let _addr = $base + $map_internal.off;
            if _ptes > $mptes {
                $map_internal.off += ($mptes << _shift) as u64;
                _ptes = $mptes;
            } else {
                $map_internal.off = 0;
                $next;
            }

            $fill($pt, $mptei, _ptes, _addr, $map_internal)?;
            $mptei += _ptes;
            $mptes -= _ptes;
        }
        $pt.memory.release();
    }
}

impl VmmDescFunc for VmmDescSPT {
    fn unmap(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_unmap(pt, ptei, ptes)
    }

    fn sparse(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_sparse(pt, ptei, ptes)
    }

    fn mem(pt: &mut MmuPt, in_ptei: u32, in_ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        let mut ptes = in_ptes;
        let mut ptei = in_ptei;

        map_internal.midx = 0;

        if VMM_TRACE {
            pr_info!("gp100_vmm_pgt_mem mem {} {}\n", ptei, ptes);
        }
        vmm_map_iter!(pt, ptes, ptei, map_internal,
                      map_internal.mem.unwrap()[map_internal.midx].size() as u64,
                      map_internal.mem.unwrap()[map_internal.midx].addr() as u64,
                      VmmDescSPT::pte,
                      map_internal.midx += 1);

        Ok(())
    }

    fn dma(pt: &mut MmuPt, in_ptei: u32, in_ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        let mut ptes = in_ptes;
        let mut ptei = in_ptei;

        map_internal.midx = 0;
        vmm_map_iter!(pt, ptes, ptei, map_internal,
                      PAGE_SIZE as u64,
                      unsafe { *map_internal.dma_base },
                      VmmDescSPT::pte,
                      map_internal.dma_base = map_internal.dma_base.wrapping_add(1));

        Ok(())
    }

    fn sgl(pt: &mut MmuPt, ptei: u32, ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        pr_err!("TODO SGL SPT\n");
        Ok(())
    }
}

pub(crate) struct VmmDescLPT {
    base: VmmDesc,
}

impl VmmDescLPT {
}

impl VmmDescFunc for VmmDescLPT {
    fn invalid(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        /* VALID_FALSE + PRIV tells the MMU to ignore corresponding SPTEs. */
        if VMM_TRACE {
            pr_info!("invalid: {} {}\n", ptei, ptes);
        }
        pt.fill64(ptei as u64 * 8, bit_u64!(5), ptes as usize)
    }

    fn unmap(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_unmap(pt, ptei, ptes)
    }

    fn sparse(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_sparse(pt, ptei, ptes)
    }

    fn mem(pt: &mut MmuPt, ptei: u32, ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        VmmDescSPT::mem(pt, ptei, ptes, map_internal)
    }
}

pub(crate) struct VmmDescPd0 {
    base: VmmDesc,
}

impl VmmDescPd0 {
    fn pte(pt: &mut MmuPt, in_ptei: u32, in_ptes: u32,
           addr: u64, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        let mut data = (addr >> 4) | map_internal.map_type;
        let mut ptei = in_ptei;
        let mut ptes = in_ptes;

        map_internal.map_type += (ptes * map_internal.ctag as u32) as u64;
        while ptes != 0 {
            pt.wo128(ptei as u64 * 16 as u64, data, 0_u64)?;
            ptei += 1;
            data += map_internal.next;
            ptes -= 1;
        }
        Ok(())
    }
}

impl VmmDescFunc for VmmDescPd0 {
    fn unmap(pt: &mut MmuPt, pdei: u32, pdes: u32) -> Result<()> {
        pt.fill128(pdei as u64 * 16, 0_u64, 0_u64, pdes as usize)
    }

    fn sparse(pt: &mut MmuPt, pdei: u32, pdes: u32) -> Result<()> {
        /* VALID_FALSE + VOL_BIG tells the MMU to treat the PDE as sparse. */
        pt.fill128(pdei as u64 * 16, bit_u64!(3), 0_u64, pdes as usize)
    }

    fn pde(pgd: &mut VmmPt, pdei: u32) -> Result<()> {
        let mut data: [u64; 2] = [ 0, 0 ];

        let pgt = pgd.pde[pdei as usize].1.as_ref().unwrap();
        let pd: &mut MmuPt =  pgd.pt[0].as_mut().unwrap();

        if !pgt.pt[0].is_none() {
            data[0] = VmmDesc::pde(pgt.pt[0].as_ref().unwrap(), data[0]);
        }

        if !pgt.pt[1].is_none() {
            data[1] = VmmDesc::pde(pgt.pt[1].as_ref().unwrap(), data[1]);
        }

        if VMM_TRACE {
            pr_info!("writing pde0 to {:#x} off: {:#x}, {:#x} {:#x}\n", pd.memory.addr()?, pdei, data[0], data[1]);
        }
        let _ = pd.memory.acquire();
        pd.wo128(pdei as u64 * 0x10, data[0], data[1])?;
        let _ = pd.memory.release();
        Ok(())
    }

    fn mem(pt: &mut MmuPt, in_ptei: u32, in_ptes: u32, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        let mut ptes = in_ptes;
        let mut ptei = in_ptei;

        map_internal.midx = 0;

        vmm_map_iter!(pt, ptes, ptei, map_internal,
                      map_internal.mem.unwrap()[map_internal.midx].size() as u64,
                      map_internal.mem.unwrap()[map_internal.midx].addr() as u64,
                      VmmDescPd0::pte,
                      map_internal.midx += 1);

        Ok(())
    }
}

pub(crate) struct VmmDescPd1 {
    base: VmmDesc,
}

impl VmmDescFunc for VmmDescPd1 {
    fn unmap(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_unmap(pt, ptei, ptes)
    }

    fn sparse(pt: &mut MmuPt, ptei: u32, ptes: u32) -> Result<()> {
        VmmDesc::pgt_sparse(pt, ptei, ptes)
    }

    fn pde(pgd: &mut VmmPt, pdei: u32) -> Result<()> {
        let mut data: u64 = 0;

        let pgt = pgd.pde[pdei as usize].1.as_ref().unwrap();
        let pd: &mut MmuPt = pgd.pt[0].as_mut().unwrap();

        if pgt.pt[0].is_none() {
            return Ok(());
        }
        data = VmmDesc::pde(pgt.pt[0].as_ref().unwrap(), data);
        if VMM_TRACE {
            pr_info!("writing pde1 to {:#x} off: {:#x}, {:#x}\n", pd.memory.addr()?, pdei, data);
        }
        let _ = pd.memory.acquire();
        pd.wo64(pdei as u64 * 8, data)?;
        let _ = pd.memory.release();
        Ok(())
    }
}


const VMM_DESC_GP100_16: [VmmDescType; 6] = [
    VmmDescType::Lpt(VmmDescLPT { base: { VmmDesc { bits: 5, size: 8,  align: 0x0100 } } }),
    VmmDescType::PgdPd0(VmmDescPd0 { base: { VmmDesc { bits: 8, size: 16, align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 9, size: 8,  align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 9, size: 8,  align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 2, size: 8,  align: 0x1000 } } }),
    VmmDescType::Empty(VmmDescNone { base : { VmmDesc { bits : 0, size: 0, align : 0 } } }),
];

const VMM_DESC_GP100_12: [VmmDescType; 6] = [
    VmmDescType::Spt(VmmDescSPT { base: { VmmDesc { bits: 9, size: 8,  align: 0x1000 } } }),
    VmmDescType::PgdPd0(VmmDescPd0 { base: { VmmDesc { bits: 8, size: 16, align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 9, size: 8,  align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 9, size: 8,  align: 0x1000 } } }),
    VmmDescType::PgdPd1(VmmDescPd1 { base: { VmmDesc { bits: 2, size: 8,  align: 0x1000 } } }),
    VmmDescType::Empty(VmmDescNone { base : { VmmDesc { bits : 0, size: 0, align : 0 } } }),
];

pub(crate) struct VmmPage {
    pub shift: u8,
    desc_array_ptr: *const VmmDescType,
    pub vmm_page_type: u8,
}

impl VmmPage {
    fn get_desc(&self, offset: usize) -> &VmmDescType {
        unsafe { self.desc_array_ptr.wrapping_add(offset).as_ref().unwrap() }
    }

    fn get_desc_bits(&self, offset: usize) -> u8 {
        unsafe { self.get_desc(offset).bits() }
    }

    fn find_pair_desc(me: &VmmPage) -> &VmmDescType {
        let ptr : *const VmmPage = me as *const VmmPage;
        unsafe { &(*((*ptr.wrapping_sub(1)).desc_array_ptr)) }
    }
}

pub(crate) const VMM_TU102: [VmmPage; 7] = [
    VmmPage { shift: 47, desc_array_ptr: &VMM_DESC_GP100_16[4], vmm_page_type: NVKM_VMM_PAGE_Sxxx },
    VmmPage { shift: 38, desc_array_ptr: &VMM_DESC_GP100_16[3], vmm_page_type: NVKM_VMM_PAGE_Sxxx },
    VmmPage { shift: 29, desc_array_ptr: &VMM_DESC_GP100_16[2], vmm_page_type: NVKM_VMM_PAGE_Sxxx },
    VmmPage { shift: 21, desc_array_ptr: &VMM_DESC_GP100_16[1], vmm_page_type: NVKM_VMM_PAGE_SVxC },
    VmmPage { shift: 16, desc_array_ptr: &VMM_DESC_GP100_16[0], vmm_page_type: NVKM_VMM_PAGE_SVxC },
    VmmPage { shift: 12, desc_array_ptr: &VMM_DESC_GP100_12[0], vmm_page_type: NVKM_VMM_PAGE_SVHx },
    VmmPage { shift: 0, desc_array_ptr: &VMM_DESC_GP100_12[5], vmm_page_type: 0 }
];

#[derive(Ord, PartialOrd, Eq, PartialEq)]
pub(crate) struct SizeAddr {
    pub size: u64,
    pub addr: u64,
}

struct VmmManaged {
    p: SizeAddr,
    n: SizeAddr,
    raw: bool
}

const NVKM_VMA_PAGE_NONE: u8 = 0x07;

pub(crate) struct VmaMutable {
    size_addr: SizeAddr,
    page: u8,
    refd: u8,
    used: bool,
    mapped: bool,
    mapref: bool,
    part: bool,
    memory: Option<InstObj>,
}

#[pin_data]
#[repr(C)]
pub(crate) struct Vma {
    vma_mut: UnsafeCell<VmaMutable>,
    sparse: bool,
    busy: bool,
    no_comp: bool,
    #[pin]
    head: ListLinks,
}

kernel::list::impl_has_list_links! {
    impl HasListLinks<0> for Vma { self.head }
}
kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for Vma { untracked; }
}

kernel::list::impl_list_item! {
    impl ListItem<0> for Vma {
        using ListLinks;
    }
}

impl Debug for Vma {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:016x} {:016x} {}{}{}{}{}{}{}{}",
               self.addr(), self.size(),
               match self.used() { true => '-', false => 'F' },
               match self.mapref() { true => 'R', false => '-' },
               match self.sparse { true => 'S', false => '-' },
               match self.page() { NVKM_VMA_PAGE_NONE => '-', x => ('0' as u8 + x) as char },
               match self.refd() { NVKM_VMA_PAGE_NONE => '-', x => ('0' as u8 + x) as char },
               match self.part() { true => 'P', false => '-' },
               '-',
               match self.mapped() { true => 'M', false => '-'})
    }
}

impl Vma {
    pub(crate) fn size(&self) -> u64 {
        unsafe { (*self.vma_mut.get()).size_addr.size }
    }

    pub(crate) fn addr(&self) -> u64 {
        unsafe { (*self.vma_mut.get()).size_addr.addr }
    }

    fn used(&self) -> bool {
        unsafe { (*self.vma_mut.get()).used }
    }

    fn set_used(&self, used: bool) {
        unsafe { (*self.vma_mut.get()).used = used };
    }

    fn mapref(&self) -> bool {
        unsafe { (*self.vma_mut.get()).mapref }
    }

    fn set_mapref(&self, mapref: bool) {
        unsafe { (*self.vma_mut.get()).mapref = mapref };
    }

    fn part(&self) -> bool {
        unsafe { (*self.vma_mut.get()).part }
    }

    fn set_part(&self, part: bool) {
        unsafe { (*self.vma_mut.get()).part = part };
    }

    fn page(&self) -> u8 {
        unsafe { (*self.vma_mut.get()).page }
    }

    fn refd(&self) -> u8 {
        unsafe { (*self.vma_mut.get()).refd }
    }

    fn mapped(&self) -> bool {
        unsafe { (*self.vma_mut.get()).mapped }
    }

    fn set_mapped(&self, mapped: bool) {
        unsafe { (*self.vma_mut.get()).mapped = mapped };
    }

    fn set_addr(&self, addr: u64) {
        unsafe { (*self.vma_mut.get()).size_addr.addr = addr };
    }

    fn add_size(&self, add: u64) {
        unsafe {
            let mut sz = (*self.vma_mut.get()).size_addr.size;
            sz += add;
            (*self.vma_mut.get()).size_addr.size = sz;
        }
    }

    fn sub_size(&self, sub: u64) {
        unsafe {
            let mut sz = (*self.vma_mut.get()).size_addr.size;
            sz -= sub;
            (*self.vma_mut.get()).size_addr.size = sz;
        }
    }

    fn set_page(&self, val: u8) {
        unsafe { (*self.vma_mut.get()).page = val; }
    }

    fn set_refd(&self, val: u8) {
        unsafe { (*self.vma_mut.get()).refd = val; }
    }

    fn new_internal(addr: u64, size: u64, mapref: bool, sparse: bool, page: u8, refd: u8,
           used: bool, part: bool, busy: bool, mapped: bool, no_comp: bool) -> Result<ListArc<Self>> {
        let unique = UniqueArc::try_pin_init(
            try_pin_init!(Self {
                head <- ListLinks::new(),
                vma_mut: VmaMutable {
                    size_addr: SizeAddr { size, addr },
                    page,
                    refd,
                    used,
                    mapped,
                    mapref,
                    part,
                    memory: None
                }.into(),
                sparse,
                busy,
                no_comp,
            }), GFP_KERNEL)?;
        Ok(ListArc::<Self>::from(unique))
    }

    fn new(addr: u64, size: u64) -> Result<ListArc<Self>> {
        Self::new_internal(addr, size, false, false,NVKM_VMA_PAGE_NONE,
                     NVKM_VMA_PAGE_NONE, false, false, false, false, false)
    }

    fn split_new(vma: &Vma, tail: u64, part: bool) -> Result<ListArc<Self>> {
        Self::new_internal(vma.addr() + (vma.size() - tail), tail,
                           vma.mapref(), vma.sparse, vma.page(), vma.refd(), vma.used(),
                           part, vma.busy, vma.mapped(), vma.no_comp)
    }

    fn new_freed(vma: &Vma) -> Result<ListArc<Self>> {
        Self::new_internal(vma.addr(), vma.size(),
                           vma.mapref(), vma.sparse, NVKM_VMA_PAGE_NONE, NVKM_VMA_PAGE_NONE, false,
                           vma.part(), vma.busy, vma.mapped(), vma.no_comp)
    }

    fn new_managed(addr: u64, size: u64) -> Result<ListArc<Self>> {
        Self::new_internal(addr, size, true, false, NVKM_VMA_PAGE_NONE, NVKM_VMA_PAGE_NONE,
                           true, false, false, false, false)
    }

    fn new_raw(addr: u64, size: u64, refd: u8) -> Result<ListArc<Self>> {
        Self::new_internal(addr, size, false, false, refd, refd,
                           true, false, false, false, true)
    }
}

const NVKM_VMM_LEVELS_MAX: usize = 5;

struct VmmIter<'a> {
    vmm_info: &'a VmmStaticInfo,
    pd: &'a mut VmmPt,
    boot_inner: Option<&'a mut VmmInner>,
    page: &'a VmmPage,
    cnt: u64,
    max: u16,
    lvl: u16,
    pte: [u32; NVKM_VMM_LEVELS_MAX],
    // pt nvkm_vmm_pt
    pt: [*mut VmmPt; NVKM_VMM_LEVELS_MAX],
    flush: usize,
}

type MapFn = fn(&mut MmuPt, u32, u32, &mut VmmMapInternal<'_>) -> Result<()>;
type RefFn = fn(&mut VmmIter<'_>, bool, u32, u32) -> Result<bool>;
type ClrFn = fn(&mut MmuPt, u32, u32) -> Result<()>;

impl<'a> VmmIter<'a> {

    fn trace_str(&self) -> Result<CString> {
        let mut outstr = c_str!("").to_cstring()?;
        for lvl in (0..=self.max).rev() {
            if lvl >= self.lvl {
                outstr = CString::try_from_fmt(fmt!("{}{:05x}:", outstr, self.pte[lvl as usize]))?;
            } else {
                outstr = CString::try_from_fmt(fmt!("{}xxxxx:", outstr))?;
            }
        }
        Ok(outstr)
    }

    fn refs_idx(&self, offset: usize) -> usize {
        match self.page.get_desc(offset) {
            VmmDescType::Spt(_) => { 1 },
            _ => { 0 }
        }
    }

    fn ref_hwpt(&mut self, pgd: &mut VmmPt, pdei: u32) -> Result<()> {
        let desc = self.page.get_desc((self.lvl - 1) as usize);
        let pg_type = self.refs_idx((self.lvl - 1) as usize);
        let pgt = &mut pgd.pde[pdei as usize].1.as_mut().unwrap();

        if VMM_TRACE {
            pr_info!("ref_hwpt: {}: {} {}\n", self.lvl, pg_type, desc.bits());
        }
        let pten = 1_u32.wrapping_shl(desc.bits() as u32);
        let size = desc.size() as usize * pten as usize;

        let zero = !pgt.sparse && !desc.has_invalid();

        pgd.refs[0] += 1;

        if VMM_TRACE {
            pr_info!("ref_hwpt: {}: {:#x}\n", pg_type, size);
        }

        let mut pt = MmuPtC::get(self.vmm_info.instmem.clone(), size as usize, (*desc).align() as usize, true)?;

        pgt.pt[pg_type] = Some(pt);

        if (!zero) {
            let overlap = match desc {
                VmmDescType::Lpt(_) => {
                    pgt.refs[1] != 0
                }
                _ => { false }
            };
            if (overlap) {
                // SPT already exists covering the same range as this LPT,
                // which means we need to be careful that any LPTEs which
                // overlap valid SPTEs are unmapped as opposed to invalid
                // or sparse, which would prevent the MMU from looking at
                // the SPTEs on some GPUs.
                //
                let mut ptei: u32 = 0;
                let mut pteb: u32 = 0;
                let mut ptes: u32 = 0;
                loop {
                    if ptei >= pten {
                        break;
                    }

                    let spte = pgt.pte[ptei as usize] & NVKM_VMM_PTE_SPTES;
                    ptes = 1;
                    ptei += 1;
                    loop {
                        if ptei >= pten {
                            break;
                        }

                        let next = pgt.pte[ptei as usize] & NVKM_VMM_PTE_SPTES;

                        if spte != next {
                            break;
                        }

                        ptes += 1;
                        ptei += 1;
                    }

                    let mut pt = pgt.pt[pg_type].as_mut().unwrap();
                    if spte == 0 {
                        if pgt.sparse {
                            desc.sparse(pt, pteb, ptes);
                        } else {
                            desc.invalid(pt, pteb, ptes);
                        }
                        for i in 0..ptes {
                            pgt.pte[(pteb + i) as usize] = 0;
                        }
                    } else {
                        desc.unmap(pt, pteb, ptes);
                        for i in 0..ptes {
                            pgt.pte[(pteb + i) as usize] |= NVKM_VMM_PTE_VALID;
                        }
                    }
                    pteb = ptei;
                }
            } else {
                if pgt.sparse {
                    Self::sparse_ptes(desc, pgt, 0, pten);
                    let mut pt = pgt.pt[pg_type].as_mut().unwrap();
                    desc.sparse(pt, 0, pten);
                } else {
                    let mut pt = pgt.pt[pg_type].as_mut().unwrap();
                    desc.invalid(pt, 0, pten);
                }
            }
        }

        //call pde function?
        if VMM_TRACE {
            pr_info!("{}: {} PDE write {}\n", &self.vmm_info.name, self.trace_str()?, desc.tname());
        }
        self.page.get_desc(self.lvl as usize).pde(pgd, pdei)?;

        self.flush_mark();
        // ptc get
        Ok(())
    }

    fn ref_swpt(&mut self, pgd: &mut VmmPt, pdei: u32) -> Result<()> {
        let desc = self.page.get_desc((self.lvl - 1) as usize);

        if VMM_TRACE {
            pr_info!("ref swpt {} {}\n", self.lvl - 1, pdei);
        }
        let is_sparse = pgd.pde[pdei as usize].0;
        let pgt = VmmPt::new(desc, is_sparse, self.lvl as usize, Some(self.page))?;

        pgd.pde[pdei as usize] = (false, Some(pgt));
        Ok(())
    }

    fn flush_mark(&mut self) {
        self.flush = core::cmp::min(self.flush, (self.max - self.lvl) as usize);
    }

    fn flush(&mut self) -> Result<()> {
        if self.flush != NVKM_VMM_LEVELS_MAX {
            if VMM_TRACE {
                pr_info!("{} {:?} flush: {}\n", &self.vmm_info.name,
                         self.trace_str()?, self.flush);
            }
            let _ = Vmm::gp100_flush_internal(&self.vmm_info, self.pd.pt[0].as_ref().unwrap().addr)?;
            self.flush = NVKM_VMM_LEVELS_MAX;
        }
        Ok(())
    }

    fn iter<'b>(pd: &'b mut VmmPt, vmm_info: &'b VmmStaticInfo,
                boot_inner: Option<&'b mut VmmInner>,
                page: &'b VmmPage, addr: u64, size: u64,
                name: &'static str, pgref: bool, pfn: bool,
                reffn: Option<RefFn>, mapfn: Option<MapFn>, map: Option<&'b VmmMap<'_>>,
                mut map_internal: Option<&'b mut VmmMapInternal<'_>>,
                clrfn: Option<ClrFn>) -> Result<u64>
    where
        'b: 'a,
    {
        let mut bits = addr >> page.shift;

        if VMM_TRACE {
            pr_info!("iter: {:#x} {:#x} {}\n", addr, size, page.shift);
        }
        let mut it = VmmIter {
            vmm_info,
            page,
            pd,
            boot_inner,
            cnt: size >> page.shift,
            flush: NVKM_VMM_LEVELS_MAX,
            max: 0,
            lvl: 0,
            pte: [0, 0, 0, 0, 0],
            pt: [core::ptr::null_mut(),
                 core::ptr::null_mut(),
                 core::ptr::null_mut(),
                 core::ptr::null_mut(),
                 core::ptr::null_mut()]
        };

        loop {
            let curr_bits = page.get_desc_bits(it.lvl as usize);
            if curr_bits == 0 {
                break;
            }
            it.pte[it.lvl as usize] = (bits as u32 & (1_u32.wrapping_shl(curr_bits as u32) - 1)) as u32;
            if VMM_TRACE {
                pr_info!("bits {}: is {} vs {}\n", it.lvl, bits, curr_bits);
            }
            bits >>= curr_bits;
            it.lvl += 1;
        }
        it.lvl -= 1;
        it.max = it.lvl;
        it.pt[it.max as usize] = it.pd as *mut VmmPt;

        it.lvl = 0;
        if VMM_TRACE {
            pr_info!("{}: {} {} {:016x} {:016x} {} {} PTEs\n", it.vmm_info.name, it.trace_str()?,
                     name, addr, size, page.shift, it.cnt);
        }
        it.lvl = it.max;

        while it.cnt != 0 {
            let mut pgt_ref = unsafe { &mut (*it.pt[it.lvl as usize]) };
            let pg_type = it.refs_idx(0);
            let pten = 1_u32.wrapping_shl(page.get_desc_bits(0) as u32);
            let ptei = it.pte[0];
            let ptes = core::cmp::min::<u64>(it.cnt, (pten - ptei) as u64 );

            if VMM_TRACE {
                pr_info!("cnt: {} {} {} {} {} {}\n", it.cnt, it.lvl, pg_type, pten, ptei, ptes);
            }
            while it.lvl != 0 {
                let pdei = it.pte[it.lvl as usize];
                let pgd = pgt_ref;

                if VMM_TRACE {
                    pr_info!("lvl: {} {}\n", it.lvl, pdei);
                }
                if pgref && pgd.pde[pdei as usize].1.is_none() {
                    it.ref_swpt(pgd, pdei)?;
                }

                it.pt[(it.lvl - 1) as usize] = pgd.pde[pdei as usize].1.as_mut().unwrap() as *mut VmmPt;

                pgt_ref = unsafe { &mut (*it.pt[(it.lvl - 1) as usize]) };

                if pgref && pgt_ref.refs[it.refs_idx((it.lvl - 1) as usize)] == 0  {
                    it.ref_hwpt(pgd, pdei)?;
                }
                it.lvl -= 1;
            }

            // call ref

            if reffn.is_none() || reffn.unwrap()(&mut it, pfn, ptei, ptes as u32)? {
                if mapfn.is_some() || clrfn.is_some() {
                    let pt = pgt_ref.pt[pg_type].as_mut().unwrap();

                    if mapfn.is_some() {
                        mapfn.unwrap()(pt, ptei, ptes as u32, map_internal.as_mut().unwrap())?;
                    } else {
                        clrfn.unwrap()(pt, ptei, ptes as u32);
                    }

                    it.flush_mark();
                }
            }

            it.pte[it.lvl as usize] += ptes as u32;
            it.cnt -= ptes;

            if it.cnt != 0 {
                while it.pte[it.lvl as usize] == 1_u32.wrapping_shl(page.get_desc_bits(it.lvl as usize) as u32) {
                    it.pte[it.lvl as usize] = 0;
                    it.lvl += 1;
                    it.pte[it.lvl as usize] += 1;
                }
            }
        }

        it.flush();
        Ok(!0_u64)
    }

    fn ref_sptes(iter: &mut VmmIter<'_>, pgt: &mut VmmPt, in_ptei: u32, in_ptes: u32) -> Result<()> {
        let mut ptes = in_ptes;
        let mut ptei = in_ptei;
        let pair = VmmPage::find_pair_desc(iter.page);
        let sptb = iter.page.get_desc_bits(0) - pair.bits();
        let desc = iter.page.get_desc(0);
        let sptn = 1 << sptb;
        let mut spti = ptei & (sptn - 1);

        let mut lpti = ptei >> sptb;
        while ptes != 0 {
            let pten = core::cmp::min(sptn - spti, ptes);
            pgt.pte[lpti as usize] += pten as u8;
            ptes -= pten;
            lpti += 1;
            spti = 0;
        }

        if pgt.refs[0] == 0 {
            return Ok(());
        }

        if VMM_TRACE {
            pr_info!("ref sptes {} {}\n", spti, lpti);
        }

        let mut pteb = ptei >> sptb;
        ptei = pteb;
        loop {
            if ptei >= lpti {
                break;
            }

            /* Skip over any LPTEs that already have valid SPTEs. */
            if pgt.pte[pteb as usize] & NVKM_VMM_PTE_VALID != 0 {
                ptes = 1;
                ptei += 1;
                loop {
                    if ptei >= lpti {
                        break;
                    }

                    if (pgt.pte[ptei as usize] & NVKM_VMM_PTE_VALID) == 0 {
                        break;
                    }
                    ptes += 1;
                    ptei += 1;
                }
                continue;
            }

            /* As there are now non-UNMAPPED SPTEs in the range covered
             * by a number of LPTEs, we need to transfer control of the
             * address range to the SPTEs.
             *
             * Determine how many LPTEs need to transition state.
             */
            pgt.pte[ptei as usize] |= NVKM_VMM_PTE_VALID;
            ptes = 1;
            ptei += 1;
            loop {
                if ptei >= lpti {
                    break;
                }

                if pgt.pte[ptei as usize] & NVKM_VMM_PTE_VALID != 0 {
                    break;
                }
                pgt.pte[ptei as usize] |= NVKM_VMM_PTE_VALID;
                ptes += 1;
                ptei += 1;

            }

            if pgt.pte[pteb as usize] & NVKM_VMM_PTE_SPARSE != 0 {

                let spti = pteb * sptn;
                let sptc = ptes * sptn;
                if VMM_TRACE {
                    pr_info!("{}: {:?} SPTE {:05x}: I -> S {} PTEs\n", &iter.vmm_info.name, iter.trace_str()?, spti, sptc);
                }
                desc.sparse(pgt.pt[1].as_mut().unwrap(), spti, sptc);
                if VMM_TRACE {
                    pr_info!("{}: {:?} LPTE {:05x}: S -> U {} PTEs\n", &iter.vmm_info.name, iter.trace_str()?, pteb, ptes);
                }
                pair.unmap(pgt.pt[0].as_mut().unwrap(), pteb, ptes);
            } else if pair.has_invalid() {
                /* MMU supports blocking SPTEs by marking an LPTE
                 * as INVALID.  We need to reverse that here.
                 */
                if VMM_TRACE {
                    pr_info!("{}: {:?} LPTE {:05x}: I -> U {} PTEs\n", &iter.vmm_info.name, iter.trace_str()?, pteb, ptes);
                }
                pair.unmap(pgt.pt[0].as_mut().unwrap(), pteb, ptes);
            }

            pteb = ptei;
        }

        Ok(())
    }

    fn unref_sptes(iter: &mut VmmIter<'_>, pgt: &mut VmmPt, in_ptei: u32, in_ptes: u32) -> Result<()> {
        let mut ptei = in_ptei;
        let mut ptes = in_ptes;
        let pair = VmmPage::find_pair_desc(iter.page);
        let sptb = iter.page.get_desc_bits(0) - pair.bits();
        let sptn = 1 << sptb;
        let mut spti = ptei & (sptn - 1);

        let mut lpti = ptei >> sptb;

        while ptes != 0 {
            let pten = core::cmp::min(sptn - spti, ptes);
            pgt.pte[lpti as usize] -= pten as u8;
            ptes -= pten;
            lpti += 1;
            spti = 0;
        }

        if pgt.refs[0] == 0 {
            return Ok(());
        }

        if VMM_TRACE {
            pr_info!("unref sptes {} {} {} {}\n", ptei, sptb, spti, lpti);
        }
        let mut pteb = ptei >> sptb;
        ptei = pteb;
        loop {
            if ptei >= lpti {
                break;
            }

            /* Skip over any LPTEs that already have valid SPTEs. */
            if pgt.pte[pteb as usize] & NVKM_VMM_PTE_SPTES != 0 {
                ptes = 1;
                ptei += 1;
                loop {
                    if ptei >= lpti {
                        break;
                    }
                    if (pgt.pte[ptei as usize] & NVKM_VMM_PTE_SPTES) == 0 {
                        break;
                    }
                    ptes += 1;
                    ptei += 1;
                }
                continue;
            }

            /* As there's no more non-UNMAPPED SPTEs left in the range
             * covered by a number of LPTEs, the LPTEs once again take
             * control over their address range.
             *
             * Determine how many LPTEs need to transition state.
             */
            pgt.pte[ptei as usize] &= !NVKM_VMM_PTE_VALID;
            ptes = 1;
            ptei += 1;
            loop {
                if ptei >= lpti {
                    break;
                }

                if pgt.pte[ptei as usize] & NVKM_VMM_PTE_SPTES != 0 {
                    break;
                }

                pgt.pte[ptei as usize] &= !NVKM_VMM_PTE_VALID;
                ptes += 1;
                ptei += 1;
            }

            if pgt.pte[pteb as usize] & NVKM_VMM_PTE_SPARSE != 0 {
                if VMM_TRACE {
                    pr_info!("{} {:?} LPTE {:05x} U -> S {} PTEs\n", &iter.vmm_info.name, iter.trace_str()?, pteb, ptes);
                }
                pair.sparse(pgt.pt[0].as_mut().unwrap(), pteb, ptes);
            } else if pair.has_invalid() {
                /* If the MMU supports it, restore the LPTE to the
                 * INVALID state to tell the MMU there is no point
                 * trying to fetch the corresponding SPTEs.
                 */
                if VMM_TRACE {
                    pr_info!("{} {:?} LPTE {:05x} U -> I {} PTEs\n", &iter.vmm_info.name, iter.trace_str()?, pteb, ptes);
                }
                pair.invalid(pgt.pt[0].as_mut().unwrap(), pteb, ptes);
            }
            pteb = ptei;
        }
        Ok(())
    }


    fn unref_pdes(iter: &mut VmmIter<'_>) -> Result<()> {
        let pg_type = iter.refs_idx((iter.lvl) as usize);
        let pgd = unsafe { &mut (*iter.pt[(iter.lvl + 1) as usize]) };
        let pgt = unsafe { &mut (*iter.pt[iter.lvl as usize]) };
        let pt = &pgt.pt[pg_type];
        let pdei = iter.pte[(iter.lvl + 1) as usize];

        if VMM_TRACE {
            pr_info!("unref_pdes: type {} pgt {} pgd refs {} lvl is {}\n", pg_type, pgt.refs[0], pgd.refs[0], iter.lvl);
        }
        iter.lvl += 1;

        pgd.refs[0] -= 1;

        if pgd.refs[0] != 0 {
            let desc = iter.page.get_desc((iter.lvl) as usize);
            if VMM_TRACE {
                pr_info!("{} {:?} PDE unmap {}\n", &iter.vmm_info.name, iter.trace_str()?, iter.page.get_desc((iter.lvl - 1) as usize).tname());
            }
            pgt.pt[pg_type] = None;

            if pgt.refs[(!pg_type) & 0x1] == 0 {
                /* PDE no longer required */
                if pgt.pt[0].is_some() {
                    if pgt.sparse {
                        desc.sparse(pgd.pt[0].as_mut().unwrap(), pdei, 1);
                        pgd.pde[pdei as usize] = (true, None);
                    } else {
                        //call unmap
                        desc.unmap(pgt.pt[0].as_mut().unwrap(), pdei, 1);
                        pgd.pde[pdei as usize] = (false, None);
                    }
                } else {
                    desc.pde(pgd, pdei);
                    pgd.pde[pdei as usize] = (false, None);
                }
            } else {
                /* PDE was pointing at dual-PTs and we're removing
                 * one of them, leaving the other in place.
                 */
                desc.pde(pgd, pdei);
            }
            iter.flush_mark();
            iter.flush();
        } else {
            Self::unref_pdes(iter)?;
        }
        if VMM_TRACE {
            pr_info!("{} {} {:?} PDE free {}\n", iter.lvl, &iter.vmm_info.name, iter.trace_str()?, iter.page.get_desc((iter.lvl - 1) as usize).tname());
        }
        if pgt.refs[(!pg_type) & 0x1] == 0 {
            // delete vmmpt somehow?
//            iter.pt[iter.lvl as usize] = core::ptr::null_mut() as *mut VmmPt;
        }
        iter.lvl -= 1;
        Ok(())
    }

    fn unref_ptes(iter: &mut VmmIter<'_>, pfn: bool, ptei: u32, ptes: u32) -> Result<bool> {
        let reftype = iter.refs_idx(0);
        let pgt = unsafe { &mut (*iter.pt[0]) };

        pgt.refs[reftype] -= ptes;

        if reftype == 1 && ((pgt.refs[0] != 0) || (pgt.refs[1] != 0)) {
            if VMM_TRACE {
                pr_info!("calling unref sptes\n");
            }
            Self::unref_sptes(iter, pgt, ptei, ptes);
        }

        if pgt.refs[reftype] == 0 {
            // unref pdees
            iter.lvl += 1;
            if VMM_TRACE {
                pr_info!("{}: {:?} {} empty\n", &iter.vmm_info.name, iter.trace_str()?, iter.page.get_desc(0).tname());
            }
            iter.lvl -= 1;
            Self::unref_pdes(iter);
            return Ok(false);
        }
        Ok(true)
    }

    fn ref_ptes(iter: &mut VmmIter<'_>, pfn: bool, ptei: u32, ptes: u32) -> Result<bool> {
        let reftype = iter.refs_idx(0);
        let pgt = unsafe { &mut (*iter.pt[0]) };

        pgt.refs[reftype] += ptes;

        if reftype == 1 {
            Self::ref_sptes(iter, pgt, ptei, ptes)?;
        }

        Ok(true)
    }

    fn boot_ptes(iter: &mut VmmIter<'_>, pfn: bool, ptei: u32, ptes: u32) -> Result<bool> {
        let reftype = iter.refs_idx(0);
        let pgt = unsafe { &mut (*iter.pt[0]) };

        pgt.pt[reftype].as_mut().unwrap().memory.boot(iter.vmm_info, iter.boot_inner.as_mut().unwrap(), iter.pd)?;
        Ok(false)
    }

    fn sparse_unref_ptes(iter: &mut VmmIter<'_>, pfn: bool, in_ptei: u32, in_ptes: u32) -> Result<bool> {
        let pgt = unsafe { &mut (*iter.pt[0]) };
        let mut ptei = in_ptei;
        let mut ptes = in_ptes;

        match iter.page.get_desc(0) {
            VmmDescType::PgdPd0(_) | VmmDescType::PgdPd1(_) => {
                while ptes != 0 {
                    pgt.pde[ptei as usize] = (false, None);
                    ptei += 1;
                    ptes -= 1;
                }
            },
            VmmDescType::Lpt(_) => {
                while ptes != 0 {
                    pgt.pte[ptei as usize] = 0;
                    ptei += 1;
                    ptes -= 1;
                }
            },
            _ => {}
        }

        Self::unref_ptes(iter, pfn, ptei, ptes)
    }

    fn sparse_ptes(desc: &VmmDescType, pgt: &mut VmmPt, in_ptei: u32, in_ptes: u32) -> Result<()> {
        let mut ptei = in_ptei;
        let mut ptes = in_ptes;

        match desc {
            VmmDescType::PgdPd0(_) | VmmDescType::PgdPd1(_) => {
                while ptes != 0 {
                    pgt.pde[ptei as usize] = (true, None);
                    ptei += 1;
                    ptes -= 1;
                }
            },
            VmmDescType::Lpt(_) => {
                while ptes != 0 {
                    pgt.pte[ptei as usize] = NVKM_VMM_PTE_SPARSE;
                    ptei += 1;
                    ptes -= 1;
                }
            },
            _ => {}
        }
        Ok(())
    }
    fn sparse_ref_ptes(iter: &mut VmmIter<'_>, pfn: bool, in_ptei: u32, in_ptes: u32) -> Result<bool> {
        let pgt = unsafe { &mut (*iter.pt[0]) };

        Self::sparse_ptes(iter.page.get_desc(0), pgt, in_ptei, in_ptes)?;

        Self::ref_ptes(iter, pfn, in_ptei, in_ptes)
    }
}

pub(crate) struct VmmStaticInfo {
    pub(crate) instmem: Arc<InstMem>,
    is_bar: bool,
    rm_bar2_pdb: u64,
    name: &'static CStr,
    bootstrapped: bool,
}

#[pin_data]
#[repr(C)]
pub(crate) struct VmmInner {
    free: RBTree<SizeAddr, Arc<Vma>>,
    root: RBTree<u64, Arc<Vma>>,
    #[pin]
    list: List<Vma>,
}

impl VmmInner {
    fn node_prev(list: &mut List<Vma>, vma: Arc<Vma>) -> Option<Arc<Vma>> {
        let cursor = list.cursor_front();

        let mut cursor = match cursor {
            None => { return None; }
            Some(x) => { x }
        };

        loop {
            if cursor.eq(vma.as_ref()) {
                break;
            }

            cursor = match cursor.next() {
                None => { return None; }
                Some(x) => { x }
            };

        }

        match cursor.prev() {
            None => None,
            Some(x) => Some(x.current().into())
        }
    }

    fn node_next(list: &mut List<Vma>, vma: Arc<Vma>) -> Option<Arc<Vma>> {
        let cursor = list.cursor_front();

        let mut cursor = match cursor {
            None => { return None; }
            Some(x) => { x }
        };

        loop {
            if cursor.eq(vma.as_ref()) {
                break;
            }

            cursor = match cursor.next() {
                None => { return None; }
                Some(x) => { x }
            };

        }

        match cursor.next() {
            None => None,
            Some(x) => {
                Some(x.current().into())
            }
        }
    }

    pub(crate) fn tail(&mut self, vma: &Vma, tail: u64, part: bool) -> Result<Arc<Vma>> {
        let newvma = Vma::split_new(vma, tail, part)?;
        vma.sub_size(tail);

        let res = newvma.clone_arc();
        self.list.push_after(&vma.head, newvma);
        Ok(res)
    }

    pub(crate) fn node_insert(&mut self, vma: Arc<Vma>) -> Result<()> {
        self.root.try_create_and_insert(vma.addr(), vma, GFP_KERNEL)?;
        Ok(())
    }

    fn node_remove(&mut self, vma: &Arc<Vma>) {
        self.root.remove(&vma.addr());
    }

    pub(crate) fn node_delete(&mut self, vma: Arc<Vma>) -> Result<()> {
        self.node_remove(&vma);
        unsafe { self.list.remove(vma.as_ref()) };
        Ok(())
    }

    pub(crate) fn free_insert(&mut self, vma: Arc<Vma>) -> Result<()> {
        self.free.try_create_and_insert(SizeAddr { size: vma.size(), addr: vma.addr() }, vma, GFP_KERNEL)?;
        Ok(())
    }

    fn free_remove(&mut self, vma: &Arc<Vma>) {
        self.free.remove(&SizeAddr { size: vma.size(), addr: vma.addr() });
    }

    pub(crate) fn free_delete(&mut self, vma: Arc<Vma>) -> Result<()> {
        self.free_remove(&vma);
        unsafe { self.list.remove(vma.as_ref()) };
        Ok(())
    }

    pub(crate) fn node_search(&mut self, addr: u64) -> Result<Arc<Vma>> {
        match self.root.get(&addr) {
            None => Err(ENOENT),
            Some(vma) => Ok(vma.clone())
        }
    }

    fn unmap_region(&mut self, vma: Arc<Vma>) -> Result<()> {
        vma.set_mapped(false);

        Ok(())
    }

    pub(crate) fn put_region(&mut self, vma: Arc<Vma>) -> Result<()> {

        let prev = Self::node_prev(&mut self.list, vma.clone());
        let next = Self::node_next(&mut self.list, vma.clone());

        match prev {
            None => {},
            Some(x) => {
                if !x.used() {
                    vma.set_addr(x.clone().addr());
                    vma.add_size(x.size());

                    unsafe {
                        self.free_delete(x)?;
                    }
                }
            }
        }

        match next {
            None => {},
            Some (x) => {
                if !x.used() {
                    vma.add_size(x.size());
                    unsafe {
                        self.free_delete(x)?;
                    }
                }
            }
        }

        self.free_insert(vma.clone())?;
        Ok(())
    }
}

pub(crate) struct VmmMapInternal<'a> {
    pub page_idx: u8,
    pub pg_shift: u8,
    pub next: u64,
    pub off: u64,
    pub map_type: u64,
    pub ctag: u64,

    pub mem: Option<&'a KVec<VramNode>>,
    pub midx: usize,

    pub dma_base: *mut bindings::dma_addr_t,

    //sgl
}

pub(crate) struct VmmMap<'a> {
    pub memory: &'a dyn Memory,
    pub offset: u64,

    pub vol: u8,
    pub ro: u8,
    pub private: u8,
    pub kind: u8,
}

pub(crate) struct VmmPromoteInfo {
    pub rsvd_lo: u64,
    pub rsvd_hi: u64,
    pub num_levels: u32,
    pub level0_phys_addr: u64,
    pub level1_phys_addr: u64,
    pub level2_phys_addr: u64
}

pub(crate) struct Vmm {
    inner: Pin<KBox<Mutex<VmmInner>>>,
    pd: Pin<KBox<Mutex<VmmPt>>>,
    sinfo: VmmStaticInfo,
    rsvd: Option<Arc<Vma>>,
    start: u64,
    limit: u64,
    name: &'static CStr,
    managed: Option<VmmManaged>,
}

impl Vmm {
    fn page(base: &GpuBase, page_idx: u8) -> &VmmPage {
        // eventually match on gpu family if this changes.
        &VMM_TU102[page_idx as usize]
    }

    fn num_pages(base: &GpuBase) -> u8 {
        // eventually match on gpu family if this changes.
        VMM_TU102.len() as u8
    }

    fn smallest_page_idx(base: &GpuBase) -> u8 {
        // eventually match on gpu family if this changes.
        (VMM_TU102.len() - 2) as u8
    }

    fn aper(target: MemTarget) -> Result<u32> {
        match target {
            MemTarget::Vram => { Ok(0) }
            MemTarget::Host => { Ok(2) }
            MemTarget::Ncoh => { Ok(3) }
            _ => Err(EINVAL)
        }
    }

    fn gp100_valid(page: &VmmPage, vma: &Vma, map: &VmmMap<'_>, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        map_internal.next = (1_u64 << page.shift) >> 4;
        map_internal.map_type = 0;

        map_internal.map_type |= bit_u64!(0);
        map_internal.map_type |= (Self::aper(map.memory.target())? as u64) << 1;
        map_internal.map_type |= (map.vol as u64) << 3;
        map_internal.map_type |= (map.private as u64) << 5;
        map_internal.map_type |= (map.ro as u64) << 6;
        map_internal.map_type |= (map.kind as u64) << 56;
        Ok(())
    }

    fn map_valid(base: &GpuBase, vma: &Vma, map: &VmmMap<'_>, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {
        // validate targets
        let page = Self::page(base, map_internal.page_idx);

        match map.memory.target() {
            MemTarget::Vram => {
                if page.vmm_page_type & NVKM_VMM_PAGE_VRAM == 0 {
                    return Err(EINVAL);
                }
            }
            MemTarget::Host | MemTarget::Ncoh => {
                if page.vmm_page_type & NVKM_VMM_PAGE_HOST == 0 {
                    return Err(EINVAL);
                }
            }
            x => { pr_err!("Illegal target {:?} in map_valid\n", x);
                   return Err(EINVAL);
            }
        }

        if (!is_aligned(vma.addr() as u64, 1_u64 << page.shift) ||
            !is_aligned(vma.size() as u64, 1_u64 << page.shift) ||
            !is_aligned(map.offset as u64, 1_u64 << page.shift) ||
            map.memory.page() < page.shift) {
            pr_err!("map valid: Illegal alignment {:#x} {:#x} {:#x} {} {}\n",
                   vma.addr(), vma.size(), map.offset, page.shift,
                   map.memory.page());
            return Err(EINVAL);
        }

        Self::gp100_valid(page, vma, map, map_internal)
    }

    fn map_choose(base: &GpuBase, vma: &Vma, map: &mut VmmMap<'_>, map_internal: &mut VmmMapInternal<'_>) -> Result<()> {

        for mp in 0..Self::num_pages(base) {
            map_internal.page_idx = mp;
            match Self::map_valid(base, &vma, map, map_internal) {
                Ok(()) => { return Ok(()); }
                _ => {}
            }
        }
        Err(EINVAL)
    }

    pub(crate) fn in_managed_range(&self, start: u64, size: u64) -> bool {
        let mgd = match &self.managed {
            None => { return false; }
            Some(m) => m
        };

        let p_start = mgd.p.addr;
        let p_end = p_start + mgd.p.size;
        let n_start = mgd.n.addr;
        let n_end = n_start + mgd.n.size;
        let end = start + size;

        if start >= p_start && end <= p_end {
            return true;
        }

        if start >= n_start && end <= n_end {
            return true;
        }
        return false;
    }

    fn ptes_unmap_put(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64, sparse: bool) -> Result<()> {
        let desc = page.get_desc(0);
        let mut clrfn = desc.invalidfn();

        if sparse {
            clrfn = desc.sparsefn();
        } else if clrfn.is_none() {
            clrfn = desc.unmapfn();
        }

        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "unmap + unref", false, false, Some(VmmIter::unref_ptes), None, None, None, clrfn)?;
        Ok(())
    }

    fn ptes_unmap(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64, sparse: bool) -> Result<()> {
        let desc = page.get_desc(0);
        let mut clrfn = desc.invalidfn();

        if sparse {
            clrfn = desc.sparsefn();
        } else if clrfn.is_none() {
            clrfn = desc.unmapfn();
        }

        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "unmap", false, false, None, None, None, None, clrfn)?;
        Ok(())
    }

    fn ptes_get(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64) -> Result<()> {
        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "ref", true, false, Some(VmmIter::ref_ptes), None, None, None, None)?;
        Ok(())
    }

    fn ptes_map(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64, map: &VmmMap<'_>, map_internal: &mut VmmMapInternal<'_>, mapfn: Option<MapFn>) -> Result<()> {
        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "map", false, false, None, mapfn, Some(map), Some(map_internal), None)?;
        Ok(())
    }

    fn ptes_get_map(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64, map: &VmmMap<'_>, map_internal: &mut VmmMapInternal<'_>, mapfn: Option<MapFn>) -> Result<()> {
        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "ref + map", true, false, Some(VmmIter::ref_ptes), mapfn, Some(map), Some(map_internal), None)?;
        Ok(())
    }

    fn ptes_put(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64) -> Result<()> {
        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "unref", false, false, Some(VmmIter::unref_ptes), None, None, None, None)?;
        Ok(())
    }

    fn ptes_sparse_get(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64) -> Result<()> {
        if (page.vmm_page_type & NVKM_VMM_PAGE_SPARSE) == 0 {
            return Err(EINVAL);
        }
        let desc = page.get_desc(0);
        let mut clrfn = desc.sparsefn();
        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "sparse ref", true, false,
                                 Some(VmmIter::sparse_ref_ptes), None, None, None, clrfn)?;
        Ok(())
    }

    fn ptes_sparse_put(pd: &mut VmmPt, sinfo: &VmmStaticInfo, page: &VmmPage, addr: u64, size: u64) -> Result<()> {
        let desc = page.get_desc(0);
        let mut clrfn = desc.invalidfn();

        if clrfn.is_none() {
            clrfn = desc.unmapfn();
        }

        let iter = VmmIter::iter(pd, sinfo, None, page, addr, size, "sparse unref", false, false,
                                 Some(VmmIter::sparse_unref_ptes), None, None, None, clrfn)?;
        Ok(())
    }

    fn ptes_sparse(pd: &mut VmmPt, sinfo: &VmmStaticInfo, in_addr: u64, in_size: u64, sparse_ref: bool) -> Result<()> {
        let mut start = in_addr;
        let mut m = 0;
        let mut size = in_size;
        let mut addr = in_addr;
        let base = &sinfo.instmem.base.clone();

        while size != 0 {
            /* Limit maximum page size based on remaining size. */
            while size < bit_u64!(Self::page(base, m).shift as usize) {
                m += 1;
            }
            let mut i = m;

            /* Find largest page size suitable for alignment. */
            while !is_aligned(addr, bit_u64!(Self::page(base, i).shift as usize)) {
                i += 1;
            }

            let block;
            let i_shift = Self::page(base, i).shift;

            /* Determine number of PTEs at this page size. */
            if (i != m) {
                /* Limited to alignment boundary of next page size. */
                let next = bit_u64!(Self::page(base, i - 1).shift);
                let part = align64(addr, next) - addr;
                if size - part >= next {
                    block = (part >> i_shift) << i_shift;
                } else {
                    block = (size >> i_shift) << i_shift;
                }
            } else {
                block = (size >> i_shift) << i_shift;
            }

            if sparse_ref {
                Self::ptes_sparse_get(pd, sinfo, Self::page(base, i), addr, block)?;
            } else {
                Self::ptes_sparse_put(pd, sinfo, Self::page(base, i), addr, block)?;
            }

            size -= block;
            addr += block;
        }
        Ok(())
    }

    fn put_internal(inner: &mut VmmInner, pt: &mut VmmPt, sinfo: &VmmStaticInfo, vma: Arc<Vma>) -> Result<()> {
        if vma.mapref() || !vma.sparse {
            let base = sinfo.instmem.base.clone();
            if vma.mapped() {
                let page = Self::page(&base, vma.refd());
                Self::ptes_unmap_put(pt, sinfo, page, vma.addr(), vma.size(), vma.sparse);
            } else {
                if vma.refd() != NVKM_VMA_PAGE_NONE {
                    let page = Self::page(&base, vma.refd());
                    Self::ptes_put(pt, sinfo, page, vma.addr(), vma.size());
                }
            }
        }

        if vma.mapped() {
            inner.unmap_region(vma.clone());
        }
        // Remove VMA from the list of allocated nodes.
        inner.node_remove(&vma);

        // Merge VMA back into the free list.

        vma.set_page(NVKM_VMA_PAGE_NONE);
        vma.set_refd(NVKM_VMA_PAGE_NONE);
        vma.set_used(false);

        inner.put_region(vma)?;
        Ok(())
    }

    pub(crate) fn get(&self, getref: bool, mapref: bool, sparse: bool,
                      shift: u8, align_val: u8, size: u64) -> Result<Arc<Vma>> {
        let mut locked_inner = self.inner.lock();
        let mut locked_pd = self.pd.lock();
        Self::get_internal(&mut locked_inner, &mut locked_pd, &self.sinfo, getref, mapref, sparse,
                           shift, align_val, size)
    }

    pub(crate) fn get_addr(&self, addr: u64) -> Result<Arc<Vma>> {
        let mut locked_inner = self.inner.lock();
        locked_inner.node_search(addr)
    }

    pub(crate) fn put_addr(&self, addr: u64) -> Result<()> {
        let mut locked_inner = self.inner.lock();
        let mut locked_pd = self.pd.lock();
        let vma = locked_inner.node_search(addr)?;
        Self::put_internal(&mut locked_inner, &mut locked_pd, &self.sinfo, vma)
    }

    pub(crate) fn put(&self, vma: Arc<Vma>) -> Result<()> {
        let mut locked_inner = self.inner.lock();
        let mut locked_pd = self.pd.lock();
        Self::put_internal(&mut locked_inner, &mut locked_pd, &self.sinfo, vma)
    }

    pub(crate) fn get_internal(inner: &mut VmmInner,
                               pd: &mut VmmPt,
                               sinfo: &VmmStaticInfo, getref: bool, mapref: bool, sparse: bool,
                               shift: u8, align_val: u8, size: u64) -> Result<Arc<Vma>> {
        let int_align;

        if getref && shift == 0 {
            pr_err!("got invalid getref with no shift\n");
            return Err(EINVAL);
        }

        let mut page_idx : Option<u8> = None;
        if shift != 0 {
            for p in 0..Self::num_pages(&sinfo.instmem.base) {
                if shift == Self::page(&sinfo.instmem.base, p).shift {
                    page_idx = Some(p);
                    break;
                }
            }

            if page_idx.is_none() {
                pr_err!("failed to find vmm page {}\n", shift);
                return Err(EINVAL);
            }
            int_align = core::cmp::max::<u8>(align_val, shift);
        } else {
            int_align = core::cmp::max::<u8>(align_val, 12);
        }

        /* Locate smallest block that can possibly satisfy the allocation. */
        let mut free: &mut RBTree<SizeAddr, Arc<Vma>> = &mut inner.free;
        let mut curs = match free.cursor_lower_bound(&SizeAddr { size: size, addr: 0 }) {
            None => { return Err(ENOSPC); }
            Some(x) => { x }
        };

        let mut curr = curs.current().1;

        let mut addr;
        let mut curr = curr.clone();

        loop {
            addr = curr.addr();

            addr = align64(addr, 1_u64 << int_align);

            let tail = curr.addr() + curr.size();

            if addr <= tail && tail - addr >= size {
                let node;
                (_, node) = curs.remove_current();
                (_, curr) = node.to_key_value();
                break;
            }
            curs = match curs.move_next() {
                None => { return Err(ENOSPC); }
                Some(x) => x
            };
            curr = curs.current().1.clone();
        }

        if addr != curr.addr() {
            let tmp = inner.tail(curr.as_ref(), curr.size() + curr.addr() - addr, false)?;
            inner.free_insert(curr.clone())?;
            curr = tmp;
        }
        if size != curr.size() {
            let tmp = inner.tail(curr.as_ref(), curr.size() - size, false)?;
            inner.free_insert(tmp)?;
        }

        if getref {
            let base = sinfo.instmem.base.clone();
            let page = Self::page(&base, page_idx.unwrap());
            Self::ptes_get(pd, sinfo, page, curr.addr(), curr.size())?;
        }

        curr.set_mapref(mapref && !getref);
        curr.set_used(true);
        if getref {
            curr.set_refd(page_idx.unwrap());
        } else {
            curr.set_refd(NVKM_VMA_PAGE_NONE);
        }
        if page_idx.is_some() {
            curr.set_page(page_idx.unwrap());
        }
        inner.node_insert(curr.clone())?;

        Ok(curr)
    }

    pub(crate) fn boot(inner: &mut VmmInner, pd: &mut VmmPt, sinfo: &mut VmmStaticInfo, vmm_start: u64, vmm_limit: u64) -> Result<()> {
        let base = sinfo.instmem.base.clone();
        let limit = vmm_limit - vmm_start;
        let page_idx = Self::smallest_page_idx(&base);
        let pg = Self::page(&base, page_idx);

        Self::ptes_get(pd, sinfo, pg, vmm_start, limit)?;

        let iter = VmmIter::iter(pd, sinfo, Some(inner), pg, vmm_start, limit, "boot", false, false,
                                 Some(VmmIter::boot_ptes), None, None, None, None);
        sinfo.bootstrapped = true;
        Ok(())
    }

    pub(crate) fn new(instmem: Arc<InstMem>,
                      lock_class: Option<(&'static LockClassKey, &'static LockClassKey)>,
                      addr: u64, size: u64,
                      vmm_type: u8,
                      is_bar: bool,
                      needs_bootstrap: bool,
                      override_pt0: Option<InstObj>,
                      join: Option<&mut InstObj>,
                      promote_vmm: bool,
                      bar2_pdb: u64,
                      name: &'static CStr) -> Result<Self> {
        let limit: u64;
        let mut bits: usize = 0;

        let page_idx = Self::smallest_page_idx(&instmem.base);

        let mut levels = 0;
        let mut desc_idx : usize = 0;
        loop {
            let desc_ref = Self::page(&instmem.base, page_idx).get_desc(desc_idx);
            let dbits = desc_ref.bits() as usize;
            if dbits == 0 {
                break;
            }

            bits += dbits;
            desc_idx += 1;
            levels += 1;
        }
        bits += Self::page(&instmem.base, page_idx).shift as usize;

        let desc_ref = Self::page(&instmem.base, page_idx).get_desc(desc_idx - 1);

        // allocate top-level page table
        //
        let pd_header: usize = 0;
        let gpu_size: usize = pd_header + desc_ref.size() as usize * (1 << desc_ref.bits());

        let mut pd = VmmPt::new(desc_ref, false, 0, None)?;

        let mut pt0: Option<MmuPt> = None;

        match override_pt0 {
            Some(x) => {
                pt0 = Some(MmuPt::wrap(x)?);
            },
            None => {
                if gpu_size > 0 {
                    pt0 = Some(MmuPtC::get(instmem.clone(), gpu_size, desc_ref.align() as usize, true)?);
                }
            }
        }

        pd.refs[0] = 1;
        pd.pt[0] = pt0;

        let vmm_start;
        let vmm_limit;
        let mut managed = None;
        if (vmm_type != NVKM_VMM_TYPE_UNMANAGED) {
            vmm_start = 0;
            vmm_limit = 1_u64 << bits;

            let p = SizeAddr { addr: 0, size: addr };

            managed = Some(VmmManaged {
                p,
                n: SizeAddr { addr: addr + size, size: vmm_limit - (addr + size)},
                raw: vmm_type == NVKM_VMM_TYPE_RAW,
            });

        } else {
            vmm_start = addr;
            vmm_limit = if size > 0 {
                addr + size
            } else {
                1_u64 << bits
            };
        }

        let mut sinfo = VmmStaticInfo {
            instmem: instmem.clone(),
            is_bar,
            rm_bar2_pdb: bar2_pdb,
            name,
            bootstrapped: false,
        };
        let mut inner = VmmInner {
            free: RBTree::new(),
            root: RBTree::new(),
            list: List::new(),
        };

        if managed.is_some() {
            let managed = &managed.as_ref().unwrap();
            let pvma = Vma::new_managed(managed.p.addr, managed.p.size)?;
            let vma = Vma::new(managed.p.size, size)?;
            let nvma = Vma::new_managed(managed.n.addr, managed.n.size)?;

            inner.node_insert(pvma.clone_arc())?;
            inner.list.push_back(pvma);

            inner.free_insert(vma.clone_arc())?;
            inner.list.push_back(vma);

            inner.node_insert(nvma.clone_arc())?;
            inner.list.push_back(nvma);
        } else {
            let vma = Vma::new(vmm_start, vmm_limit - vmm_start)?;
            inner.free_insert(vma.clone_arc())?;
            inner.list.push_back(vma);
        }

        if needs_bootstrap {
            let _ = Self::boot(&mut inner, &mut pd, &mut sinfo, vmm_start, vmm_limit);
        }

        match join {
            None => {},
            Some(join) => { let _ = Self::join_internal(&pd, join, vmm_limit); }
        }

        let mut rsvd = None;
        if promote_vmm {
            rsvd = Some(Self::get_internal(&mut inner, &mut pd, &sinfo, true, false, false, 0x1d, 32, 0x20000000)?);
        }

        let lock_name;
        let (inner_lock_key, pd_lock_key) = match lock_class {
            Some((x, y)) => { lock_name = Some(name); (x, y) },
            None => { lock_name = None ; (static_lock_class!(), static_lock_class!()) }
        };

        Ok(Self {
            inner: KBox::pin_init(Mutex::new(inner,
                                             runtime_optional_name!(lock_name),
                                             inner_lock_key), GFP_KERNEL)?,
            pd: KBox::pin_init(Mutex::new(pd,
                                          runtime_optional_name!(lock_name),
                                          pd_lock_key), GFP_KERNEL)?,
            sinfo,
            managed,
            rsvd,
            start: vmm_start,
            limit: vmm_limit,
            name,
        })
    }

    pub(crate) fn getpd0_addr(&self) -> Result<u64> {
        let pd = self.pd.lock();
        Ok(pd.pde[0].1.as_ref().unwrap().pt[0].as_ref().unwrap().addr)
    }

    pub(crate) fn map(&self, vma: Arc<Vma>, map: &mut VmmMap<'_>) -> Result<()> {
        let mut locked_pd = self.pd.lock();
        Self::map_internal(&mut locked_pd, &self.sinfo, vma, map)
    }

    pub(crate) fn map_internal(pd: &mut VmmPt, sinfo: &VmmStaticInfo, vma: Arc<Vma>, map: &mut VmmMap<'_>) -> Result<()> {
        let mut map_internal = VmmMapInternal {
            page_idx: 0,
            pg_shift: 0,
            next: 0,
            off: 0,
            map_type: 0,
            ctag: 0,
            mem: None,
            midx: 0,
            dma_base: core::ptr::null_mut(),
        };
        let base = &sinfo.instmem.base.clone();

        if vma.page() == NVKM_VMA_PAGE_NONE && vma.refd() == NVKM_VMA_PAGE_NONE {
            Self::map_choose(base, &vma, map, &mut map_internal)?;
        } else {
            if vma.refd() != NVKM_VMA_PAGE_NONE {
                map_internal.page_idx = vma.refd();
            } else {
                map_internal.page_idx = vma.page();
            }

            Self::map_valid(base, &vma, map, &mut map_internal)?;
        }

        let page = Self::page(base, map_internal.page_idx);
        let desc = page.get_desc(0);

        map_internal.pg_shift = page.shift;

        pr_info!("mapping post valid {} {} {} {}\n", map_internal.page_idx, vma.refd(), vma.page(), map_internal.pg_shift);

        map_internal.off = map.offset;

        let mapfn;
        match map.memory.obj_type() {
            MemObjType::VRAM => {
                let vram : *const VramObj = map.memory as *const dyn Memory as *const VramObj;
                map_internal.mem = unsafe { Some(&(*vram).nodes) };

                while map_internal.off != 0 {
                    let size: u64 = map_internal.mem.unwrap()[map_internal.midx].size();
                    if size > map_internal.off {
                        break;
                    }
                    map_internal.off -= size;
                    map_internal.midx += 1;
                }
                mapfn = desc.memfn();
            },
            MemObjType::DMA => {
                let dmamemobj : *const DmaMemObj = map.memory as *const dyn Memory as *const DmaMemObj;
                map_internal.dma_base = unsafe { (*dmamemobj).addr_array.offset((map.offset >> PAGE_SHIFT) as isize) };
                map_internal.off = map.offset & (PAGE_MASK as u64);
                mapfn = desc.dmafn();
            },
            MemObjType::SGL => {
                let sglmemobj : *const SglMemObj = map.memory as *const dyn Memory as *const SglMemObj;
                mapfn = desc.sglfn();
            }
            MemObjType::INST => {
                let instobj: *const InstObj = map.memory as *const dyn Memory as *const InstObj;
                map_internal.mem = unsafe { Some(&(*instobj).vram.nodes) };
                while map_internal.off != 0 {
                    let size: u64 = map_internal.mem.unwrap()[map_internal.midx].size();
                    if size > map_internal.off {
                        break;
                    }
                    map_internal.off -= size;
                    map_internal.midx += 1;
                }
                mapfn = desc.memfn();
            }
        };

        if vma.refd() == NVKM_VMA_PAGE_NONE {
            Self::ptes_get_map(pd, sinfo, page, vma.addr(), vma.size(), map, &mut map_internal, mapfn)?;
            vma.set_refd(map_internal.page_idx);
        } else {
            Self::ptes_map(pd, sinfo, page, vma.addr(), vma.size(), map, &mut map_internal, mapfn)?;
            //ptes map
        }
        vma.set_mapped(true);
        Ok(())
    }

    pub(crate) fn part(&self, inst: &mut InstObj) -> Result<()> {
        let mut locked_inner = self.inner.lock();
        inst.fill64(0x200, 0x0, 0x2)
    }

    fn gp100_join(instobj: &mut InstObj, addr: u64, target: MemTarget, vmm_limit: u64) -> Result<()> {
        let mut base: u64 = bit_u64!(10) | bit_u64!(11); // VER2 | 64KiB

        // replay TODO
        match target {
            MemTarget::Vram => { base |= 0_u64 << 0; }
            MemTarget::Host => { base |= 2_u64 << 0;
                                 base |= bit_u64!(2); // VOL
            }
            MemTarget::Ncoh => { base |= 3_u64 << 0; }
            (x) => { pr_err!("Unknown mem target {:?}\n", x); return Err(EINVAL); }
        }

        base |= addr;

        instobj.acquire()?;
        instobj.wr64(0x200_u64, base)?;
        instobj.wr64(0x208_u64, vmm_limit - 1)?;
        instobj.release();

        instobj.acquire();
        let mask: u64 = bit_u64!(0);

        instobj.wr32(0x21c_u64, 0)?;

        for i in 0_u64..64_u64 {
            if mask & (1_u64.wrapping_shl(i as u32)) != 0 {
                instobj.wr32(0x2a4_u64 + (i * 0x10), (base >> 32) as u32)?;
                instobj.wr32(0x2a0_u64 + (i * 0x10), (base & 0xffffffff) as u32)?;
            } else {
                instobj.wr32(0x2a4_u64 + (i * 0x10), 1)?;
                instobj.wr32(0x2a0_u64 + (i * 0x10), 1)?;
            }
            instobj.wr32(0x2a8_u64 + (i * 0x10), 0)?;
        }

        instobj.wr32(0x298_u64, (mask & 0xffffffff) as u32)?;
        instobj.wr32(0x29c_u64, (mask >> 32) as u32)?;

        instobj.release();
        Ok(())
    }

    fn gp100_flush_internal(sinfo: &VmmStaticInfo, base_addr: u64) -> Result<()> {
        let mut flush_type: u32 = 0;

        flush_type |= 0x1;  /* PAGE_ALL */

        if sinfo.is_bar {
            flush_type |= 0x6;
        }

        let addr: u32;
        if sinfo.rm_bar2_pdb == 0 {
            addr = (base_addr >> 8) as u32;
        } else {
            addr = (sinfo.rm_bar2_pdb >> 8) as u32;
        }

        let bar = sinfo.instmem.base.bar.try_access().ok_or(ENXIO)?;

        bar.try_writel(addr, 0xb830a0)?;

        bar.try_writel(0x0, 0xb830a4)?;
        bar.try_writel(0x80000000 | flush_type, 0xb830b0)?;

        timer_msec!({
            if bar.try_readl(0xb830b0)? != 0x80000000 {
                break;
            }
        }, 2000, &sinfo.instmem.base.timer);

        Ok(())
    }

    fn join_internal(pd: &VmmPt, inst: &mut InstObj, limit: u64) -> Result<()> {
        let pd = pd.pt[0].as_ref().unwrap();

        Self::gp100_join(inst, pd.addr, pd.memory.target(), limit)
    }

    pub(crate) fn join(&self, inst: &mut InstObj) -> Result<()> {
        let mut pd = self.pd.lock();
        Self::join_internal(&mut pd, inst, self.limit)
    }

    fn flush(&self) -> Result<()> {
        let pd = self.pd.lock();
        Self::gp100_flush_internal(&self.sinfo, pd.pt[0].as_ref().unwrap().addr)
    }

    pub(crate) fn dump(&self) {
        let mut locked_inner = self.inner.lock();
        pr_info!("VMM {}: {:#x} {:#x}\n", self.name, self.start, self.limit);
        for vma in &locked_inner.list {
            pr_info!("{:?}\n", *vma);
        }
    }

    pub(crate) fn limit(&self) -> Result<u64> {
        Ok(self.limit)
    }

    pub(crate) fn unmap_addr(&self, addr: u64) -> Result<()> {
        let mut locked_inner = self.inner.lock();
        let mut pd = self.pd.lock();
        let vma = locked_inner.node_search(addr)?;

        let base = self.sinfo.instmem.base.clone();
        let page = Self::page(&base, vma.refd());
        if vma.mapref() {
            Self::ptes_unmap_put(&mut pd, &self.sinfo, page, vma.addr(), vma.size(), vma.sparse);
            vma.set_refd(NVKM_VMA_PAGE_NONE);
        } else {
            Self::ptes_unmap(&mut pd, &self.sinfo, page, vma.addr(), vma.size(), vma.sparse);
        }

        locked_inner.unmap_region(vma);
        Ok(())
    }

    pub(crate) fn get_promote_info(&self) -> Result<VmmPromoteInfo> {

        let rsvd = match &self.rsvd {
            None => { return Err(EINVAL); }
            Some(rsvd) => rsvd
        };

        let mut locked_pd = self.pd.lock();

        let mut num_levels = 3;

        if locked_pd.pde[0].1.is_none() {
            pr_info!("got pde 0 is none FAIL\n");
            return Err(EINVAL);
        }
        if locked_pd.pde[0].1.as_ref().unwrap().pde[0].1.is_none() {
            num_levels = 2;
        }

        let addr0: u64 = locked_pd.pt[0].as_ref().unwrap().addr;
        let addr1: u64 = locked_pd.pde[0].1.as_ref().unwrap().pt[0].as_ref().unwrap().addr;

        let mut addr2: u64 = 0;
        if num_levels == 3 {
            addr2 = locked_pd.pde[0].1.as_ref().unwrap().pde[0].1.as_ref().unwrap().pt[0].as_ref().unwrap().addr;
        }

        Ok(VmmPromoteInfo {
            rsvd_lo: rsvd.addr(),
            rsvd_hi: rsvd.addr() + rsvd.size() - 1,
            num_levels,
            level0_phys_addr: addr0,
            level1_phys_addr: addr1,
            level2_phys_addr: addr2,
        })
    }

    fn raw_page_index(&self, size: u64, shift: u8) -> Result<u8> {
        if shift == 0 {
            return Err(EINVAL);
        }

        for p in 0..Self::num_pages(&self.sinfo.instmem.base) {
            if shift == Self::page(&self.sinfo.instmem.base, p).shift {
                return Ok(p);
            }
        }

        pr_err!("failed to find vmm page {}\n", shift);
        return Err(EINVAL);
    }

    pub(crate) fn raw_sparse(&self, addr: u64, size: u64, sparse_ref: bool) -> Result<()> {
        if !self.in_managed_range(addr, size) {
            return Err(EINVAL);
        }

        let mut locked_pd = self.pd.lock();
        Self::ptes_sparse(&mut locked_pd, &self.sinfo, addr, size, sparse_ref)
    }

    pub(crate) fn raw_get(&self, shift: u8, addr: u64, size: u64) -> Result<()> {
        if !self.in_managed_range(addr, size) {
            return Err(EINVAL);
        }

        let page_idx = self.raw_page_index(size, shift)?;

        let page = Self::page(&self.sinfo.instmem.base, page_idx);

        let mut locked_pd = self.pd.lock();
        Self::ptes_get(&mut locked_pd, &self.sinfo, page, addr, size)
    }

    pub(crate) fn raw_put(&self, shift: u8, addr: u64, size: u64) -> Result<()> {

        if !self.in_managed_range(addr, size) {
            return Err(EINVAL);
        }
        let page_idx = self.raw_page_index(size, shift)?;

        let page = Self::page(&self.sinfo.instmem.base, page_idx);

        let mut locked_pd = self.pd.lock();
        Self::ptes_put(&mut locked_pd, &self.sinfo, page, addr, size)
    }

    pub(crate) fn raw_map(&self, shift: u8, addr: u64, size: u64, map: &mut VmmMap<'_>) -> Result<()> {

        if !self.in_managed_range(addr, size) {
            return Err(EINVAL);
        }
        let page_idx = self.raw_page_index(size, shift)?;
        let page = Self::page(&self.sinfo.instmem.base, page_idx);

        let vma = Vma::new_raw(addr, size, page_idx)?;

        self.map(vma.clone_arc(), map)?;
        Ok(())
    }

    pub(crate) fn raw_unmap(&self, shift: u8, addr: u64, size: u64, sparse: bool) -> Result<()> {
        if !self.in_managed_range(addr, size) {
            return Err(EINVAL);
        }
        let page_idx = self.raw_page_index(size, shift)?;
        let page = Self::page(&self.sinfo.instmem.base, page_idx);
        let mut locked_pd = self.pd.lock();
        Self::ptes_unmap(&mut locked_pd, &self.sinfo, page, addr, size, sparse);
        Ok(())
    }
}

impl Drop for Vmm {
    fn drop(&mut self) {
        match &self.rsvd {
            None => {}
            Some(r) => { self.put(r.clone()); }
        }
    }
}
