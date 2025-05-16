// SPDX-License-Identifier: GPL-2.0

//! GSP Sequencer implementation for Pre-hopper GSP boot sequence.

use core::mem::size_of;
use kernel::time::Delta;
use kernel::prelude::*;

use crate::driver::Bar0;
use crate::falcon::{gsp::Gsp, sec2::Sec2, Falcon};
use crate::firmware::Firmware;
use crate::gsp::GspSequencerInfo;
use crate::nvfw::r570_144 as fw;
use crate::util::wait_on;

use kernel::pr_info;

impl_from_bytes!(fw::GSP_SEQUENCER_BUFFER_CMD);
const CMD_SIZE: usize = size_of::<fw::GSP_SEQUENCER_BUFFER_CMD>();

/// GSP Sequencer Command types with payload data
/// Commands have an opcode and a opcode-dependent struct.
pub(crate) enum GspSeqCmd {
    RegWrite(fw::GSP_SEQ_BUF_PAYLOAD_REG_WRITE),
    RegModify(fw::GSP_SEQ_BUF_PAYLOAD_REG_MODIFY),
    RegPoll(fw::GSP_SEQ_BUF_PAYLOAD_REG_POLL),
    DelayUs(fw::GSP_SEQ_BUF_PAYLOAD_DELAY_US),
    RegStore(fw::GSP_SEQ_BUF_PAYLOAD_REG_STORE),
    CoreReset,
    CoreStart,
    CoreWaitForHalt,
    CoreResume,
}

impl GspSeqCmd {
    /// Creates a new GspSeqCmd from a firmware GSP_SEQUENCER_BUFFER_CMD
    pub(crate) fn from_fw_cmd(cmd: &fw::GSP_SEQUENCER_BUFFER_CMD) -> Result<Self> {
        match cmd.opCode {
            // SAFETY: In the below unsafe accesses, we're using the union field
            //         that corresponds to the opCode
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_WRITE => {
                Ok(GspSeqCmd::RegWrite(unsafe { cmd.payload.regWrite }))
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_MODIFY => {
                Ok(GspSeqCmd::RegModify(unsafe { cmd.payload.regModify }))
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_POLL => {
                Ok(GspSeqCmd::RegPoll(unsafe { cmd.payload.regPoll }))
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_DELAY_US => {
                Ok(GspSeqCmd::DelayUs(unsafe { cmd.payload.delayUs }))
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_REG_STORE => {
                Ok(GspSeqCmd::RegStore(unsafe { cmd.payload.regStore }))
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESET => Ok(GspSeqCmd::CoreReset),
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_START => Ok(GspSeqCmd::CoreStart),
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT => {
                Ok(GspSeqCmd::CoreWaitForHalt)
            }
            fw::GSP_SEQ_BUF_OPCODE_GSP_SEQ_BUF_OPCODE_CORE_RESUME => Ok(GspSeqCmd::CoreResume),
            _ => Err(EINVAL),
        }
    }

    pub(crate) fn new(data: &[u8]) -> Result<Self> {
        let fw_cmd = fw::GSP_SEQUENCER_BUFFER_CMD::from_bytes(data)?;
        let cmd = Self::from_fw_cmd(&fw_cmd)?;

        if data.len() < cmd.size_bytes() {
            pr_err!("data is not enough for command.\n");
            return Err(EINVAL);
        }

        Ok(cmd)
    }

    /// Get the size of this command in bytes, the command consists of
    /// a 4-byte opcode, and a variable-sized payload.
    pub(crate) fn size_bytes(&self) -> usize {
        let opcode_size = size_of::<fw::GSP_SEQ_BUF_OPCODE>();
        match self {
            // Each simple command type just adds 4 bytes (opcode_size) for the header
            GspSeqCmd::CoreReset
            | GspSeqCmd::CoreStart
            | GspSeqCmd::CoreWaitForHalt
            | GspSeqCmd::CoreResume => opcode_size,

            // For commands with payloads, add the payload size in bytes
            GspSeqCmd::RegWrite(_) => opcode_size + size_of::<fw::GSP_SEQ_BUF_PAYLOAD_REG_WRITE>(),
            GspSeqCmd::RegModify(_) => {
                opcode_size + size_of::<fw::GSP_SEQ_BUF_PAYLOAD_REG_MODIFY>()
            }
            GspSeqCmd::RegPoll(_) => opcode_size + size_of::<fw::GSP_SEQ_BUF_PAYLOAD_REG_POLL>(),
            GspSeqCmd::DelayUs(_) => opcode_size + size_of::<fw::GSP_SEQ_BUF_PAYLOAD_DELAY_US>(),
            GspSeqCmd::RegStore(_) => opcode_size + size_of::<fw::GSP_SEQ_BUF_PAYLOAD_REG_STORE>(),
        }
    }
}

pub(crate) struct GspSequencer<'a> {
    pub seq_info: GspSequencerInfo,
    pub bar: &'a Bar0,
    pub sec2_falcon: &'a Falcon<Sec2>,
    pub gsp_falcon: &'a Falcon<Gsp>,
    pub libos_dma_handle: u64,
    pub fw: &'a Firmware,
}

pub(crate) trait GspSeqCmdRunner {
    fn run(&self, sequencer: &GspSequencer<'_>) ->  Result;
}

impl GspSeqCmdRunner for fw::GSP_SEQ_BUF_PAYLOAD_REG_WRITE {
    // TODO: Can we return Result instead of  Result?
    fn run(&self, sequencer: &GspSequencer<'_>) ->  Result {
        pr_debug!("RegWrite: addr=0x{:x}, val=0x{:x}\n", self.addr, self.val);
        let addr = self.addr as usize;
        let val = self.val;
        let _ = sequencer.bar.try_write32(val, addr);
        Ok(())
    }
}

impl GspSeqCmdRunner for fw::GSP_SEQ_BUF_PAYLOAD_REG_MODIFY {
    fn run(&self, sequencer: &GspSequencer<'_>) ->  Result {
        pr_info!(
            "RegModify: addr=0x{:x}, mask=0x{:x}, val=0x{:x}\n",
            self.addr,
            self.mask,
            self.val
        );

        let addr = self.addr as usize;
        if let Ok(temp) = sequencer.bar.try_read32(addr) {
            let _ = sequencer.bar.try_write32((temp & !self.mask) | self.val, addr);
        }
        Ok(())
    }
}

impl GspSeqCmdRunner for fw::GSP_SEQ_BUF_PAYLOAD_REG_POLL {
    fn run(&self, sequencer: &GspSequencer<'_>) ->  Result {
        pr_debug!(
            "RegPoll: addr=0x{:x}, mask=0x{:x}, val=0x{:x}, timeout=0x{:x}, error=0x{:x}\n",
            self.addr,
            self.mask,
            self.val,
            self.timeout,
            self.error
        );

        let addr = self.addr as usize;
        let mut timeout_us = self.timeout as i64;

        // Default timeout to 4 seconds
        timeout_us = if timeout_us == 0 { 4000000 } else { timeout_us };

        // First read (TODO: not needed?)
        sequencer.bar.try_read32(addr)?;

        // Poll the requested register with requested timeout.
        // wait_on() unwraps the closure's Option<R> return value
        // and returns a Result<R>.
        wait_on(Delta::from_micros(timeout_us), || {
            sequencer.bar.try_read32(addr).ok().and_then(|current| {
                if (current & self.mask) == self.val {
                    Some(())
                } else {
                    None
                }
            })
        })?;
        Ok(())
    }
}

impl GspSeqCmdRunner for fw::GSP_SEQ_BUF_PAYLOAD_DELAY_US {
    fn run(&self, _sequencer: &GspSequencer<'_>) ->  Result {
        pr_info!("DelayUs: val=0x{:x}\n", self.val);
        // TODO[DLAY]: replace with udelay() or equivalent once available.
        let _: Result = wait_on(Delta::from_micros(self.val as i64), || None);
        Ok(())
    }
}

impl GspSeqCmdRunner for fw::GSP_SEQ_BUF_PAYLOAD_REG_STORE {
    fn run(&self, sequencer: &GspSequencer<'_>) ->  Result {
        let addr = self.addr as usize;

        // TODO: We just read the register value here, but we don't
        // use it anywhere. This is like Nouveau. Can we just not
        // implement this? For now it is implemented for testing.
        let val = sequencer.bar.try_read32(addr)?;

        pr_info!(
            "RegStore: addr=0x{:x}, index=0x{:x}, value={:?}\n",
            self.addr,
            self.index,
            val
        );

        Ok(())
    }
}

impl GspSeqCmdRunner for GspSeqCmd {
    fn run(&self, seq: &GspSequencer<'_>) ->  Result {
        match self {
            // TODO: Can reduce the number of "impl GspSeqCmdRunner" above, if
            // moving logic into this block itself. But may not be worth it if
            // the logic for the individual commands is long.
            GspSeqCmd::RegWrite(cmd) => cmd.run(seq),
            GspSeqCmd::RegModify(cmd) => cmd.run(seq),
            GspSeqCmd::RegPoll(cmd) => cmd.run(seq),
            GspSeqCmd::DelayUs(cmd) => cmd.run(seq),
            GspSeqCmd::RegStore(cmd) => cmd.run(seq),
            GspSeqCmd::CoreReset => {
                pr_info!("CoreReset\n");
                seq.gsp_falcon.reset(seq.bar)?;

                // Nouveau also sets NV_PFALCON_FBIF_CTL bit7 to 0 leaving other
                // bits as it is. Then it sets NV_PFALCON_FALCON_DMACTL to 0x00000000.
                // TODO: Shall we move this into falcon.reset() itself?
                seq.gsp_falcon.dma_reset(seq.bar)?;
                Ok(())
            }
            GspSeqCmd::CoreStart => {
                pr_info!("CoreStart\n");
                seq.gsp_falcon.start(seq.bar)?;
                Ok(())
            }
            GspSeqCmd::CoreWaitForHalt => {
                pr_info!("CoreWaitForHalt\n");
                seq.gsp_falcon.wait_till_halted(seq.bar)?;
                Ok(())
            }
            GspSeqCmd::CoreResume => {
                pr_info!("CoreResume\n");
                // At this point, 'SEC2-RTOS' has been loaded into SEC2 by the sequencer
                // but neither SEC2-RTOS nor GSP-RM is running yet. This part of the
                // sequencer will start both.

                // First prepare the GSP for resume.
                seq.gsp_falcon.reset(seq.bar)?;
                seq.gsp_falcon.write_mailboxes(
                    seq.bar,
                    Some(seq.libos_dma_handle as u32),
                    Some((seq.libos_dma_handle >> 32) as u32),
                )?;

                // Now start the SEC2, this will resume GSP-RM on the GSP.
                seq.sec2_falcon.start(seq.bar)?;

                // Check if GSP-RM resumed.
                seq.gsp_falcon.check_reload_completed(seq.bar, Delta::from_secs(2))?;

                // Check for any errors in the SEC2 mailbox registers.
                let mbox0 = seq.sec2_falcon.read_mailbox0(seq.bar)?;
                if mbox0 != 0 {
                    pr_err!("Sequencer: sec2 errors: {:?}\n", mbox0);
                    return Err(EIO);
                }

                // Write the OS version to the GSP falcon.
                seq.gsp_falcon.write_os_version(seq.bar, seq.fw.gsp_desc.app_version())?;

                // Check if the RISC-V core is active, return error if not
                if !seq.gsp_falcon.is_riscv_active(seq.bar)? {
                    pr_err!("Sequencer: RISC-V core is not active\n");
                    return Err(EIO);
                }
                Ok(())
            }
        }
    }
}

pub(crate) struct GspSeqIter<'a> {
    cmd_data: &'a [u8],
    current_offset: usize, // Tracking the current position
    total_cmds: u32,
    cmds_processed: u32,
}

impl<'a> Iterator for GspSeqIter<'a> {
    type Item = Result<GspSeqCmd>;

    fn next(&mut self) -> Option<Self::Item> {
        // Stop if we've processed all commands or reached the end of data
        if self.cmds_processed >= self.total_cmds || self.current_offset >= self.cmd_data.len() {
            return None;
        }

        // Check if we have enough data for opcode
        let opcode_size = size_of::<fw::GSP_SEQ_BUF_OPCODE>();
        if self.current_offset + opcode_size > self.cmd_data.len() {
            return Some(Err(EINVAL));
        }

        let offset = self.current_offset;

        // Handle command creation based on available data,
        // zero-pad if necessary (since last command may not be full size).
        let mut buffer = [0u8; CMD_SIZE];
        let copy_len = if offset + CMD_SIZE <= self.cmd_data.len() {
            CMD_SIZE
        } else {
            self.cmd_data.len() - offset
        };
        buffer[..copy_len].copy_from_slice(&self.cmd_data[offset..offset + copy_len]);
        let cmd_result = GspSeqCmd::new(&buffer);

        cmd_result.map_or_else(
            |_| {
                pr_err!("Error parsing command at offset {}", offset);
                None
            },
            |cmd| {
                self.current_offset += cmd.size_bytes();
                self.cmds_processed += 1;
                Some(Ok(cmd))
            },
        )
    }
}

impl<'a> IntoIterator for &'a GspSequencer<'a> {
    type Item = Result<GspSeqCmd>;
    type IntoIter = GspSeqIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        let cmd_data = &self.seq_info.cmd_data[..];

        GspSeqIter {
            cmd_data,
            current_offset: 0,
            total_cmds: self.seq_info.info.cmdIndex,
            cmds_processed: 0,
        }
    }
}

impl<'a> GspSequencer<'a> {
    pub(crate) fn new(
        seq_info: GspSequencerInfo,
        bar: &'a Bar0,
        sec2_falcon: &'a Falcon<Sec2>,
        gsp_falcon: &'a Falcon<Gsp>,
        libos_dma_handle: u64,
        fw: &'a Firmware,
    ) -> Result<Self> {
        Ok(GspSequencer {
            seq_info,
            bar,
            sec2_falcon,
            gsp_falcon,
            libos_dma_handle,
            fw,
        })
    }

    pub(crate) fn run(&self) ->  Result {
        pr_info!("Running CPU Sequencer commands\n");

        for cmd_result in self {
            match cmd_result {
                Ok(cmd) => cmd.run(self)?,
                Err(e) => {
                    pr_err!(
                        "Error running command at index {}\n",
                        self.seq_info.info.cmdIndex
                    );
                    return Err(e);
                }
            }
        }

        pr_info!("CPU Sequencer commands completed successfully\n");

        Ok(())
    }
}
