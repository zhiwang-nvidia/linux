#![allow(dead_code)]
use kernel::devres::Devres;
use kernel::sync::Arc;
use kernel::prelude::*;
use kernel::delay::sleep;
use core::time::Duration;
use crate::driver::Bar0;

const NV04_PTIMER_TIME_0 : usize = 0x9400;
const NV04_PTIMER_TIME_1 : usize = 0x9410;

pub(crate) struct Timer {
    bar: Arc<Devres<Bar0>>,
}

pub(crate) struct TimerWait {
    limit: u64,
    time0: u64,
    time1: u64,
    reads: u64,
}
    
impl Timer {
    pub(crate) fn new(bar: Arc<Devres<Bar0>>) -> Result<Self> {
        let ret = Self {
            bar,
        };
        ret.time(0x1234)?;

        pr_info!("timer: {}\n", ret.read()?);
        sleep(Duration::from_micros(10));
        pr_info!("timer: {}\n", ret.read()?);
        Ok(ret)
    }

    pub(crate) fn read(&self) -> Result<u64> {
        let bar = self.bar.try_access().ok_or(ENXIO)?;
        let mut ret : u64;
        loop {
            let hi = bar.readl(NV04_PTIMER_TIME_1);
            let lo = bar.readl(NV04_PTIMER_TIME_0);

            ret = ((hi as u64) << 32) | (lo as u64);
            if hi == bar.readl(NV04_PTIMER_TIME_1) {
                break;
            }

        }
        Ok(ret)
    }

    pub(crate) fn time(&self, time: u64) -> Result<()> {
        let bar = self.bar.try_access().ok_or(ENXIO)?;        
        bar.writel((time >> 32) as u32, NV04_PTIMER_TIME_1);
        bar.writel((time & 0xffffffff) as u32, NV04_PTIMER_TIME_0);
        Ok(())
    }
}

impl TimerWait {
    pub(crate) fn new(nsec: u64) -> Self {
        Self {
            limit: nsec,
            time0: 0,
            time1: 0,
            reads: 0,
        }
    }

    pub(crate) fn test(&mut self, timer: &Timer) -> Result<i64> {
        let time = timer.read()?;

        if self.reads == 0 {
            self.time0 = time;
            self.time1 = time;
        }

        if self.time1 == time {
            self.reads += 1;
            if self.reads == 16 {
                return Err(ETIME);
            }
        } else {
            self.time1 = time;
            self.reads = 1;
        }

        if self.time1 - self.time0 > self.limit {
            return Err(ETIME);
        }

        Ok(self.time1 as i64 - self.time0 as i64)
    }
}

/// Creates a timer loop checking a condition in nanoseconds
#[macro_export]
macro_rules! timer_nsec {
    ($cond:block, $time:expr, $timer:expr) => {
        {
            let mut twait = TimerWait::new($time);
            let mut taken = 0;
            loop {
                $cond;
                
                match twait.test($timer) {
                    Err(x) => {
                        pr_warn!("timeout\n");
                        return Err(x);
                    },
                    Ok(x) => {
                        taken = x;
                        if x < 0 {
                            break;
                        }
                    }
                }
            }
            taken
        }
    }
}

/// Creates a timer loop checking a condition in microseconds
#[macro_export]
macro_rules! timer_usec {
    ($cond: block, $time:expr, $timer:expr) => {
        timer_nsec!($cond, ($time * 1000), $timer)
    }
}

/// Creates a timer loop checking a condition in milliseconds
#[macro_export]
macro_rules! timer_msec {
    ($cond: block, $time:expr, $timer:expr) => {
        timer_nsec!($cond, ($time * 1000 * 1000), $timer)
    }
}
