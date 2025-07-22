// SPDX-License-Identifier: GPL-2.0
//
// RM Control implementation for nova-core
// RM control commands are used to query and configure various GPU resources.

use crate::driver::Bar0;
use crate::gsp::{GspCmdq, GspMessageElement, GspStaticConfigInfo, GspRpcHeader, GspMsgHeader};
use crate::nvfw::r570_144 as fw;
use crate::sbuffer::{SBuffer, SBufferIteratorMut};
use crate::util::wait_on_result;
use kernel::devres::Devres;
use kernel::prelude::*;
use kernel::time::Delta;

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
    copied_size: usize,
    header: RmControlHeader,
    params: Option<&'a [u8]>,
}

impl<'a> GspMessageElement for RmControlMessage<'a> {
    fn copy_to_sbuf(&mut self, sbuf: &mut SBufferIteratorMut<'_, '_>) -> Result {
        let header_len = size_of::<RmControlHeader>();
        let mut copied = 0;

        if self.copied_size == 0 {
            // Write the header
            let header_bytes = unsafe {
                core::slice::from_raw_parts(
                    &self.header as *const RmControlHeader as *const u8,
                    header_len,
                )
            };
            sbuf.write_slice(header_bytes)?;
            copied += header_len;
        }

        // Write params if present
        if let Some(params) = self.params {
            let max_params_size: usize = (16 * 0x1000) - size_of::<GspMsgHeader>() - size_of::<GspRpcHeader>() - copied;
            let mut start = 0;

            if self.copied_size != 0 {
                start = self.copied_size - header_len;
            }

            let end = (start + max_params_size).min(params.len());
            let params_slice = &params[start..end];

            sbuf.write_slice(params_slice)?;
            copied += params_slice.len();
        }

        self.copied_size += copied;

        Ok(())
    }

    fn size(&self) -> usize {
        size_of::<RmControlHeader>() + self.header.params_size as usize
    }

    fn remain_size(&self) -> Result<usize> {
        if self.copied_size < self.size() {
            Ok(self.size() - self.copied_size)
        } else {
            Err(EINVAL)
        }
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
pub(crate) struct RmControl {
    h_client: u32,
    h_subdevice: u32,
}

impl RmControl {
    /// Create new RM control instance
    pub(crate) fn new(
        gsp_info: &GspStaticConfigInfo,
    ) -> Self {
        Self {
            h_client: gsp_info.h_internal_client,
            h_subdevice: gsp_info.h_internal_subdevice,
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
            h_client: self.h_client,
            h_object: self.h_subdevice,
            cmd,
            status: 0,
            params_size: params_size as u32,
            flags: 0,
        };

        // Create message wrapper
        let mut msg = RmControlMessage {
            copied_size: 0,
            header,
            params: params.map(|p| p.to_bytes()),
        };

        // Send the command using GSP RPC.
        cmdq.send(bar, fw::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL, &mut msg)?;

        pr_info!(
            "RM Control: Sent command {:#x} with {} bytes params\n",
            cmd,
            params_size
        );

        // Wait for response
        let response = wait_on_result(Delta::from_secs(5), || {
            match cmdq
                .receive_wait_ignore::<RmControlGspResponse>(Delta::from_secs(5), fw::NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL)
            {
                Ok(response) => Some(Ok(response)),
                Err(EAGAIN) => None,
                Err(e) => Some(Err(e)),
            }
        })?;

        // Check for RM errors
        if response.header.status != 0 {
            pr_err!(
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
