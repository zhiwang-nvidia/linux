// SPDX-License-Identifier: GPL-2.0

use kernel::device;
use kernel::firmware::Firmware;
use kernel::prelude::*;

use crate::dma::DmaObject;
use crate::firmware::BinHdr;

use super::RmRiscvUCodeDesc;

pub(crate) struct RiscvFirmware {
    pub ucode: DmaObject,
    pub code_offset: u32,
    pub data_offset: u32,
    pub manifest_offset: u32,
    #[allow(dead_code)] // Will be used when bootloader version reporting is implemented
    pub app_version: u32,
}

impl RiscvFirmware {
    pub(crate) fn new(dev: &device::Device<device::Bound>, fw: &Firmware) -> Result<Self> {
        let hdr = fw
            .data()
            .get(0..size_of::<BinHdr>())
            .ok_or(EINVAL)
            .and_then(BinHdr::from_bytes)?;

        let riscv_desc = {
            let offset = hdr.header_offset as usize;

            fw.data()
                .get(offset..offset + size_of::<RmRiscvUCodeDesc>())
                .ok_or(EINVAL)
                .and_then(RmRiscvUCodeDesc::from_bytes)?
        };

        pr_info!("{:?}\n", hdr);
        pr_info!("{:?}\n", riscv_desc);

        let ucode = {
            let fw_start = hdr.data_offset as usize;
            let fw_size = hdr.data_size as usize;

            DmaObject::from_data(
                dev,
                fw.data().get(fw_start..fw_start + fw_size).ok_or(EINVAL)?,
            )?
        };

        Ok(Self {
            ucode,
            code_offset: riscv_desc.monitor_code_offset,
            data_offset: riscv_desc.monitor_data_offset,
            manifest_offset: riscv_desc.manifest_offset,
            app_version: riscv_desc.app_version,
        })
    }
}
