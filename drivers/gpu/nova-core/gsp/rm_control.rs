// SPDX-License-Identifier: GPL-2.0
//
// RM Control implementation for nova-core
// RM control commands are used to query and configure various GPU resources.

use crate::driver::Bar0;
use crate::gsp::{GspCmdq, GspMessageElement, GspStaticConfigInfo};
use crate::nvfw::r570_144 as fw;
use crate::sbuffer::{SBuffer, SBufferIteratorMut};
use crate::util::wait_on_result;
use kernel::device;
use kernel::devres::Devres;
use kernel::prelude::*;
use kernel::time::Delta;
use kernel::{dev_err, dev_info};

// RM Control RPC header structure
#[repr(C)]
#[derive(Debug)]
pub(crate) struct RmControlHeader {
    h_client: u32,
    h_object: u32,
    cmd: u32,
    status: u32,
    params_size: u32,
    flags: u32,
}
impl GspMessageElement for RmControlHeader {}

// Response wrapper that holds both header and data
pub(crate) struct RmControlGspResponse {
    pub header: RmControlHeader,
    pub data: KVec<u8>,
}

impl GspMessageElement for RmControlGspResponse {
    fn new_from_sbuf(sbuf: &SBuffer<'_>) -> Result<Self> {
        // Read RmControlRpc header
        let header = RmControlHeader::new_from_sbuf(sbuf)?;

        // Read variable-length data after header
        let data = sbuf.read_kvec(size_of::<RmControlHeader>())?;

        Ok(RmControlGspResponse { header, data })
    }
}

// Message wrapper for sending (header + params)
struct RmControlMessage<'a> {
    header: RmControlHeader,
    params: Option<&'a [u8]>,
}

impl<'a> GspMessageElement for RmControlMessage<'a> {
    fn copy_to_sbuf(&self, sbuf: &mut SBufferIteratorMut<'_, '_>) -> Result {
        // Write the header
        let header_bytes = unsafe {
            core::slice::from_raw_parts(
                &self.header as *const RmControlHeader as *const u8,
                size_of::<RmControlHeader>(),
            )
        };
        sbuf.write_slice(header_bytes)?;

        // Write params if present
        if let Some(params) = self.params {
            sbuf.write_slice(params)?;
        }

        Ok(())
    }

    fn size(&self) -> usize {
        size_of::<RmControlHeader>() + self.header.params_size as usize
    }
}

// Trait for input parameters
pub(crate) trait RmControlParams {
    fn to_bytes(&self) -> &[u8];
}

// Trait for RM control response message element
pub(crate) trait RmControlMessageElement: Sized {
    /// Parse response from bytes
    fn from_bytes(data: &[u8]) -> Result<Self>;
}

// Main RM Control API struct
pub(crate) struct RmControl<'a> {
    gsp_info: &'a GspStaticConfigInfo,
    dev: &'a device::Device<device::Bound>,
}

#[expect(dead_code)]
impl<'a> RmControl<'a> {
    /// Create new RM control instance
    pub(crate) fn new(
        gsp_info: &'a GspStaticConfigInfo,
        dev: &'a device::Device<device::Bound>,
    ) -> Self {
        Self {
            gsp_info,
            dev,
        }
    }

    /// Send an RM control command and get typed response
    pub(crate) fn send<P: RmControlParams, T: RmControlMessageElement>(
        &mut self,
        bar: &Devres<Bar0>,
        cmdq: &mut GspCmdq,
        cmd: u32,
        params: Option<&P>,
    ) -> Result<T> {
        let params_size = params.map_or(0, |p| p.to_bytes().len());

        // Create RPC header
        let header = RmControlHeader {
            h_client: self.gsp_info.h_internal_client,
            h_object: self.gsp_info.h_internal_subdevice,
            cmd,
            status: 0,
            params_size: params_size as u32,
            flags: 0,
        };

        // Create message wrapper
        let msg = RmControlMessage {
            header,
            params: params.map(|p| p.to_bytes()),
        };

        // Send the command using GSP RPC.
        cmdq.send(bar, fw::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL, &msg)?;

        dev_info!(
            self.dev,
            "RM Control: Sent command {:#x} with {} bytes params\n",
            cmd,
            params_size
        );

        // Wait for response
        let response = wait_on_result(Delta::from_secs(5), || {
            match cmdq
                .receive::<RmControlGspResponse>(fw::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL)
            {
                Ok(response) => Some(Ok(response)),
                Err(EAGAIN) => None,
                Err(e) => Some(Err(e)),
            }
        })?;

        // Check for RM errors
        if response.header.status != 0 {
            dev_err!(
                self.dev,
                "RM Control: Command {:#x} failed with status {:#x}\n",
                cmd,
                response.header.status
            );
            return Err(EIO);
        }

        // Parse and return data of the expected type.
        T::from_bytes(&response.data)
    }
}
