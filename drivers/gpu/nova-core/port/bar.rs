#![allow(dead_code)]
use kernel::prelude::*;
use kernel::sync::Arc;
use kernel::io::Io;
use kernel::static_lock_class;
use kernel::c_str;
use crate::gsp::GspManager;
use crate::mmu::memory::InstObj;
use crate::mmu::memory::InstMem;
use crate::mmu::memory::VramObj;
use crate::mmu::memory::Memory;
use crate::mmu::vmm::{Vmm, NVKM_VMM_TYPE_UNMANAGED};

const PAGE_SIZE: usize = 4096;

#[allow(unused)]
pub(crate) struct BarN {
    bar2: bool,
    inst: InstObj,
    //    vmm
}

impl BarN {
    pub(crate) fn new(inst: InstObj, bar2: bool) -> Result<BarN> {
        Ok(Self {
            bar2,
            inst,
        })
    }
}

// needs a spinlock
pub(crate) struct Bar {
    /* flush bar */

    bar1: BarN,
    bar2: Option<BarN>,

    bar1_vmm: Vmm,
    bar2_vmm: Option<Vmm>,

    bar2_flush_phys: Io<PAGE_SIZE>,
    bar2_flush_fb_zero: Option<InstObj>,
}

impl Bar {

    pub(crate) fn new(instmem: Arc<InstMem>, gsp: Arc<dyn GspManager>, bar1_size: u64,
                      bar2_size: Option<u64>,
                      bar2_phys_addr: u64) -> Result<Bar> {
        let bar1_inner_lock_class = static_lock_class!();
        let bar2_inner_lock_class = static_lock_class!();
        let bar1_pd_lock_class = static_lock_class!();
        let bar2_pd_lock_class = static_lock_class!();

        let mut o_bar2 = None;
        let mut o_bar2_vmm = None;
        let mut o_bar2_flush = None;
        match bar2_size {
            Some(bar2_size) => {
                let mut bar2 = BarN::new(InstObj::new(instmem.clone(), 0x1000, 0, false, true)?, true)?;

                pr_info!("BAR 2 INIT VMM {:#x}\n", bar2_size / 2);
                let bar2_vmm = Vmm::new(instmem.clone(), Some((bar2_inner_lock_class, bar2_pd_lock_class)), 0, bar2_size / 2, NVKM_VMM_TYPE_UNMANAGED, true, true, None, Some(&mut bar2.inst), false, gsp.get_bar_pdb(2), c_str!("bar2"))?;

                gsp.update_bar_pde(1, bar2_vmm.getpd0_addr()?, 47)?;

                let obj = VramObj::wrap(0, PAGE_SIZE)?;

                let mut iobj = VramObj::vram_kmap(obj, instmem.clone())?;

                iobj.kmap(&bar2_vmm, instmem.clone())?;

                o_bar2_flush = Some(iobj);
                o_bar2 = Some(bar2);
                o_bar2_vmm = Some(bar2_vmm);
            }
            _ => {}
        }

        let obj = InstObj::new(instmem.clone(), 0x1000, 0, false, true)?;
        let mut bar1 = BarN::new(obj, false)?;

        pr_info!("BAR 1 INIT VMM {:#x}\n", bar1_size);
        let vramobj = InstObj::wrap(instmem.clone(), VramObj::wrap(gsp.get_bar_pdb(1) as usize, 0x1000)?)?;
        let bar1_vmm = Vmm::new(instmem.clone(), Some((bar1_inner_lock_class, bar1_pd_lock_class)), 0, bar1_size, NVKM_VMM_TYPE_UNMANAGED, true, false, Some(vramobj), Some(&mut bar1.inst), false, 0, c_str!("bar1"))?;

        let bar2_flush_phys_mode = unsafe { Io::<PAGE_SIZE>::new(bar2_phys_addr as usize, PAGE_SIZE)? };

        Ok(Self {
            bar1,
            bar2: o_bar2,
            bar1_vmm,
            bar2_vmm: o_bar2_vmm,
            bar2_flush_phys: bar2_flush_phys_mode,
            bar2_flush_fb_zero: o_bar2_flush,
        })
    }

    pub(crate) fn bar1_vmm(&self) -> &Vmm {
        &self.bar1_vmm
    }

    pub(crate) fn bar2_vmm(&self) -> Option<&Vmm> {
        self.bar2_vmm.as_ref()
    }

    pub(crate) fn flush(&self) -> Result<()> {
        match &self.bar2_flush_fb_zero {
            Some(flush) => { let _ = flush.raw_rd32(0); }
            None => { let _ = self.bar2_flush_phys.readl(0); }
        }
        Ok(())
    }
}

impl Drop for Bar {
    fn drop(&mut self) {
        let _ = self.bar1_vmm.part(&mut self.bar1.inst);

        if self.bar2_vmm.is_none() == false {
            let _ = self.bar2_vmm.as_mut().unwrap().part(&mut self.bar2.as_mut().unwrap().inst);
        }
    }

}
