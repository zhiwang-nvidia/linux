#![allow(dead_code)]

use kernel::devres::Devres;
use kernel::error::Result;
use kernel::sync::Arc;
use kernel::pr_info;

use crate::driver::Bar0;
use crate::port::timer::Timer;

use crate::gsp::GspCmdq;

/// Structure holding the base pre-GSP boot GPU pieces
pub(crate) struct GpuBase {
     /// MMIO mapping of PCI BAR 0
    pub bar: Arc<Devres<Bar0>>,
    pub timer: Timer,
}

pub(crate) struct GspManager0 {
    gpu_base: Arc<GpuBase>,
    bar1_pdb: u64,
    bar2_pdb: u64,
}

impl GspManager0 {
    pub(crate) fn new(gpu_base: Arc<GpuBase>, bar1_pdb: u64, bar2_pdb: u64) -> Result<Self> {
        pr_info!("new {:x} {:x}\n", bar1_pdb, bar2_pdb);
        Ok(Self {
            gpu_base,
            bar1_pdb,
            bar2_pdb,
        })
    }
}

pub(crate) trait GspManager: Send + Sync {
    fn get_bar_pdb(&self, bar: u8) -> u64;
    fn update_bar_pde(&self, cmdq: &mut GspCmdq, bar: u32, addr: u64, shift: u32) -> Result<()>;
}

impl GspManager for GspManager0 {
    fn get_bar_pdb(&self, bar: u8) -> u64 {
        if bar == 1 {
            pr_info!("get bar1 pdb {:x}\n", self.bar1_pdb);
            self.bar1_pdb
        } else {
            pr_info!("get bar2 pdb {:x}\n", self.bar2_pdb);
            self.bar2_pdb
        }
    }
    fn update_bar_pde(&self, _cmdq: &mut GspCmdq, _bar: u32, _addr: u64, _shift: u32) -> Result<()> {
        Ok(())
    }
}

pub(crate) fn rounddown(x: usize, y: usize) -> usize {
    x - (x % y)
}

pub(crate) fn roundup(x: usize, y: usize) -> usize {
    (x + (y - 1) / y) * y
}

pub(crate) fn order_base_2(n: usize) -> u32 {
    if n > 1 {
        (n - 1).ilog2() + 1
    } else {
        0
    }
}

pub(crate) fn align(value: usize, alignment: usize) -> usize {
    (value + alignment - 1) & !(alignment - 1)
}

pub(crate) fn align64(value: u64, alignment: u64) -> u64 {
    (value + alignment - 1) & !(alignment - 1)
}

pub(crate) fn is_aligned<T: Into<u64>>(x: T, a: T) -> bool {
    let x = x.into();
    let a = a.into();
    (x & (a - 1)) == 0
}
