// SPDX-License-Identifier: GPL-2.0

use core::alloc::Layout;
use core::mem::MaybeUninit;

use kernel::alloc::allocator::Kmalloc;
use kernel::alloc::Allocator;
use kernel::asm;
use kernel::device;
use kernel::devres::Devres;
use kernel::dma::CoherentAllocation;
use kernel::pci;
use kernel::pr_info;
use kernel::prelude::*;
use kernel::time::Delta;
use kernel::transmute::{AsBytes, FromBytes};
use kernel::{dma_read, dma_write};

use crate::dma::DmaObject;
use crate::driver::Bar0;
use crate::falcon::{gsp::Gsp, sec2::Sec2, Falcon};
use crate::fb::FbLayout;
use crate::firmware::Firmware;
use crate::nvfw::r570_144 as fw;
use crate::regs::NV_PGSP_QUEUE_HEAD;
use crate::sbuffer::{SBuffer, SBufferIteratorMut};
use crate::util::wait_on_result;

pub(crate) mod sequencer;

pub(crate) const GSP_PAGE_SHIFT: usize = 12;
pub(crate) const GSP_PAGE_SIZE: usize = 1 << GSP_PAGE_SHIFT;
pub(crate) const GSP_HEAP_SHIFT: u64 = 1 << 20;

unsafe impl FromBytes for fw::GSP_ARGUMENTS_CACHED {}
unsafe impl AsBytes for fw::GSP_ARGUMENTS_CACHED {}
unsafe impl FromBytes for fw::GspFwWprMeta {}
unsafe impl AsBytes for fw::GspFwWprMeta {}
unsafe impl FromBytes for fw::GspSystemInfo {}
unsafe impl AsBytes for fw::GspSystemInfo {}

// We provide this trait because not all our structs are Sized so therefore the
// AsBytes and FromBytes traits don't work. However we can provide default
// implementations for all structs that are Sized, which we do here.
//
// This also allows us to create a convenient internal representation of a
// message which is only converted to bytes when actually doing the call. See the
// registry for an example.
pub(crate) trait GspMessageElement {
    fn copy_to_sbuf(&self, sbuf: &mut SBufferIteratorMut<'_, '_>) -> Result
    where
        Self: Sized,
    {
        let cmd_slice =
            unsafe { core::slice::from_raw_parts(self as *const Self as *const u8, self.size()) };
        sbuf.write_slice(cmd_slice)
    }

    fn new_from_sbuf(sbuf: &SBuffer<'_>) -> Result<Self>
    where
        Self: Sized,
    {
        return unsafe {
            let mut result = MaybeUninit::<Self>::uninit();
            let result_ptr = result.as_mut_ptr() as *mut u8;
            let result_slice = core::slice::from_raw_parts_mut(result_ptr, size_of::<Self>());
            sbuf.read(0, result_slice)?;
            Ok(result.assume_init())
        };
    }

    // Creates a new struct by copying bytes from the given byte slice.
    // SAFETY: Assumes the given byte slice is a valid representation of self.
    fn new_from_slice(slice: &[u8]) -> Result<Self>
    where
        Self: Sized,
    {
        if slice.len() < size_of::<Self>() {
            return Err(EINVAL);
        }

        Ok(unsafe { core::ptr::read(slice.as_ptr() as *const Self) })
    }

    fn size(&self) -> usize
    where
        Self: Sized,
    {
        return size_of::<Self>();
    }
}

pub(crate) struct GspSequencerInfo {
    info: fw::rpc_run_cpu_sequencer_v17_00,
    cmd_data: KVec<u8>,
}

impl GspMessageElement for GspSequencerInfo {
    fn new_from_sbuf(sbuf: &SBuffer<'_>) -> Result<Self> {
        let info = fw::rpc_run_cpu_sequencer_v17_00::new_from_sbuf(sbuf)?;
        let cmd_data = sbuf.read_kvec(size_of::<fw::rpc_run_cpu_sequencer_v17_00>())?;
        Ok(GspSequencerInfo { info, cmd_data })
    }
}

pub(crate) struct GspStaticConfigInfo {
    pub gpu_name: [u8; 40],
}

impl GspMessageElement for GspStaticConfigInfo {
    fn new_from_sbuf(sbuf: &SBuffer<'_>) -> Result<Self> {
        let gpu_name_str = unsafe {
            let static_info_ptr = sbuf.as_ptr::<fw::GspStaticConfigInfo_t>(0)?;
            (*static_info_ptr)
                .gpuNameString
                .get(
                    0..=(*static_info_ptr)
                        .gpuNameString
                        .iter()
                        .position(|&b| b == 0)
                        .unwrap_or((*static_info_ptr).gpuNameString.len() - 1),
                )
                .and_then(|bytes| CStr::from_bytes_with_nul(bytes).ok())
                .and_then(|cstr| cstr.to_str().ok())
                .unwrap_or("invalid utf8")
        };

        let mut gpu_name = [0u8; 40];
        let bytes = gpu_name_str.as_bytes();
        let copy_len = core::cmp::min(bytes.len(), gpu_name.len());
        gpu_name[..copy_len].copy_from_slice(&bytes[..copy_len]);
        gpu_name[copy_len] = b'\0';

        Ok(GspStaticConfigInfo { gpu_name })
    }
}

// This next section contains constants and structures hand-coded from the GSP
// headers We could replace these with bindgen versions, but that's a bit of a
// pain because they basically end up pulling in the world (ie. definitions for
// every rpc method). So for now the hand-coded ones are fine. They are just
// structs so we can easily move to bindgen generated ones if/when we want to.

// A GSP RPC header
#[repr(C)]
#[derive(Debug)]
struct GspRpcHeader {
    header_version: u32,
    signature: u32,
    length: u32,
    function: u32,
    rpc_result: u32,
    rpc_result_private: u32,
    sequence: u32,
    cpu_rm_gfid: u32,
}
impl GspMessageElement for GspRpcHeader {}

// A GSP message element header
#[repr(C)]
#[derive(Debug)]
struct GspMsgHeader {
    auth_tag_buffer: [u8; 16],
    aad_buffer: [u8; 16],
    checksum: u32,
    sequence: u32,
    elem_count: u32,
    pad: u32,
}
impl GspMessageElement for GspMsgHeader {}

// These next two structs come from msgq_priv.h. Hopefully the will never
// need updating once the ABI is stabalised.
#[repr(C)]
#[derive(Debug)]
struct MsgqTxHeader {
    version: u32,    // queue version
    size: u32,       // bytes, page aligned
    msg_size: u32,   // entry size, bytes, must be power-of-2, 16 is minimum
    msg_count: u32,  // number of entries in queue
    write_ptr: u32,  // message id of next slot
    flags: u32,      // if set it means "i want to swap RX"
    rx_hdr_off: u32, // Offset of msgqRxHeader from start of backing store
    entry_off: u32,  // Offset of entries from start of backing store
}

#[repr(C)]
#[derive(Debug)]
struct MsgqRxHeader {
    read_ptr: u32, // message id of last message read
}

// There is no struct defined for this in the open-gpu-kernel-source headers.
// Instead it is defined by code in GspMsgQueuesInit().
#[repr(C)]
#[derive(Debug)]
struct Msgq {
    tx: MsgqTxHeader,
    rx: MsgqRxHeader,
    _pad: [u8; GSP_PAGE_SIZE - size_of::<MsgqTxHeader>() - size_of::<MsgqRxHeader>()],
    msgq: [[u8; GSP_PAGE_SIZE]; 0x3f],
}

#[repr(C)]
#[derive(Debug)]
struct GspMem {
    ptes: [u8; GSP_PAGE_SIZE],
    cpuq: Msgq,
    gspq: Msgq,
}

impl GspMessageElement for fw::GspStaticConfigInfo_t {}
impl GspMessageElement for fw::rpc_run_cpu_sequencer_v17_00 {}

// Needed for CoherentAllocation
unsafe impl FromBytes for GspMem {}
unsafe impl AsBytes for GspMem {}

// SAFETY: this hack isn't :-) Only required until Nova core can boot GSP.
unsafe impl Send for GspCmdq<'_> {}

pub(crate) struct GspCmdq<'a> {
    msg_count: u32,
    seq: u32,
    gsp_mem: CoherentAllocation<GspMem>,
    nr_ptes: u32,
    bar: &'a Devres<Bar0>,
    gsp_falcon: &'a Falcon<Gsp>,
    sec2_falcon: &'a Falcon<Sec2>,
    libos_dma_handle: u64,
    fw: &'a Firmware,
}

impl<'a> GspCmdq<'a> {
    // This is equivalent to gsp_shared_init()
    fn new(
        dev: &device::Device<device::Bound>,
        bar: &'a Devres<Bar0>,
        gsp_falcon: &'a Falcon<Gsp>,
        sec2_falcon: &'a Falcon<Sec2>,
        libos_dma_handle: u64,
        fw: &'a Firmware,
    ) -> Result<GspCmdq<'a>> {
        let mut gsp_mem =
            CoherentAllocation::<GspMem>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;

        let nr_ptes = size_of::<GspMem>() >> GSP_PAGE_SHIFT;
        build_assert!((size_of::<GspMem>() >> GSP_PAGE_SHIFT) * size_of::<u64>() <= GSP_PAGE_SIZE);

        // Basically the same as create_pte_array() but we don't skip the first
        // PTE.
        // SAFETY: By the above build_assert which ensures the number of ptes
        // fits in the GSP_PAGE_SIZE allocated for GspMem.ptes
        let ptes = unsafe {
            let ptr = gsp_mem.start_ptr_mut() as *mut u64;
            core::slice::from_raw_parts_mut(ptr, nr_ptes)
        };

        for (i, pte) in ptes.iter_mut().enumerate() {
            *pte = gsp_mem.dma_handle() as u64 + ((i as u64) << GSP_PAGE_SHIFT);
        }

        let msg_count = ((0x40000 - GSP_PAGE_SIZE) / GSP_PAGE_SIZE) as u32;
        dma_write!(gsp_mem[0].cpuq.tx.version = 0)?;
        dma_write!(gsp_mem[0].cpuq.tx.size = 0x40000)?;
        dma_write!(gsp_mem[0].cpuq.tx.entry_off = GSP_PAGE_SIZE as u32)?;
        dma_write!(gsp_mem[0].cpuq.tx.msg_size = GSP_PAGE_SIZE as u32)?;
        dma_write!(gsp_mem[0].cpuq.tx.msg_count = msg_count)?;
        dma_write!(gsp_mem[0].cpuq.tx.write_ptr = 0)?;
        dma_write!(gsp_mem[0].cpuq.tx.flags = 1)?;

        // TODO: Hard-coded for now because offset_of!() isn't stable for nested types
        dma_write!(gsp_mem[0].cpuq.tx.rx_hdr_off = 32)?;

        Ok(GspCmdq {
            msg_count,
            seq: 0,
            gsp_mem,
            nr_ptes: nr_ptes as u32,
            bar,
            gsp_falcon,
            sec2_falcon,
            libos_dma_handle,
            fw,
        })
    }

    // We need the next four accessors because the dma_read macro is failable
    // and uses `?` which requires any calling function to return a Result<>.
    // However in the first instance a dma_read failure probably needs to be dealt with
    // by the function trying to do the read, so we need the accessors to permit that.
    //
    // Of course at the moment we "deal" with errors by panicing...
    //
    // I think we need to update the dma macro's to return a Result<u32>
    fn cpu_wptr(self: &Self) -> Result<u32> {
        dma_read!(self.gsp_mem[0].cpuq.tx.write_ptr)
    }

    fn gsp_rptr(self: &Self) -> Result<u32> {
        dma_read!(self.gsp_mem[0].gspq.rx.read_ptr)
    }

    fn cpu_rptr(self: &Self) -> Result<u32> {
        dma_read!(self.gsp_mem[0].cpuq.rx.read_ptr)
    }

    fn gsp_wptr(self: &Self) -> Result<u32> {
        dma_read!(self.gsp_mem[0].gspq.tx.write_ptr)
    }

    // Returns the numbers of pages free for sending an RPC to GSP.
    fn get_free_tx_pages(self: &Self) -> u32 {
        let wptr = self.cpu_wptr().unwrap();
        let rptr = self.gsp_rptr().unwrap();
        let mut free = rptr + self.msg_count - wptr - 1;

        if free >= self.msg_count {
            free -= self.msg_count;
        }

        free
    }

    // Returns the number of pages the GSP has written to the queue.
    fn get_used_rx_pages(self: &Self) -> u32 {
        let rptr = self.cpu_rptr().unwrap();
        let wptr = self.gsp_wptr().unwrap();
        let mut used = wptr + self.msg_count - rptr;
        if used >= self.msg_count {
            used -= self.msg_count;
        }

        used
    }

    fn calculate_checksum(sbuf: &SBuffer<'_>) -> u32 {
        let mut sum64: u64 = 0;
        {
            let mut iter = sbuf.iter().rev();
            while let Some(byte) = iter.next() {
                sum64 = sum64.rotate_left(8) ^ (byte as u64);
            }
        }
        ((sum64 >> 32) as u32) ^ (sum64 as u32)
    }

    fn alloc_cmd_sbuffer<'b>(self: &mut Self, cmd_size: usize) -> Result<SBuffer<'b>> {
        let msg_size = cmd_size.div_ceil(GSP_PAGE_SIZE);

        while self.get_free_tx_pages() < msg_size as u32 {}
        let wptr = self.cpu_wptr().unwrap() as usize;
        let mut ptr =
            unsafe { core::ptr::addr_of_mut!((*self.gsp_mem.start_ptr_mut()).cpuq.msgq[wptr]) };

        // Simple case where the queue doesn't wrap
        if wptr + msg_size <= 0x3f {
            let slice: &mut [u8] = unsafe {
                core::slice::from_raw_parts_mut(ptr as *mut u8, msg_size * GSP_PAGE_SIZE)
            };

            return SBuffer::<'b>::new((slice, None));
        }

        // First slice contains the remaining free pages in the queue
        let slice_1: &mut [u8] = unsafe {
            core::slice::from_raw_parts_mut(ptr as *mut u8, (0x3f - wptr) * GSP_PAGE_SIZE)
        };
        ptr = unsafe { core::ptr::addr_of_mut!((*self.gsp_mem.start_ptr_mut()).cpuq.msgq[0]) };
        pr_info!("msg_size {} wptr {}\n", msg_size, wptr);
        let slice_2: &mut [u8] = unsafe {
            core::slice::from_raw_parts_mut(
                ptr as *mut u8,
                (msg_size - 0x3f + wptr) * GSP_PAGE_SIZE,
            )
        };
        return SBuffer::<'b>::new((slice_1, Some(slice_2)));
    }

    pub(crate) fn send<A: GspMessageElement>(
        self: &mut Self,
        function: u32,
        cmd: &A,
    ) -> Result<()> {
        let mut msg_header = GspMsgHeader {
            auth_tag_buffer: [0; 16],
            aad_buffer: [0; 16],
            checksum: 0,
            sequence: self.seq,
            elem_count: 1,
            pad: 0,
        };
        let mut rpc = GspRpcHeader {
            header_version: 0x03000000,
            signature: 0x43505256,
            length: 0,
            function,
            rpc_result: 0xffffffff,
            rpc_result_private: 0xffffffff,
            sequence: 0,
            cpu_rm_gfid: 0,
        };

        self.seq += 1;
        rpc.length = (size_of::<GspRpcHeader>() + cmd.size()) as u32;

        let mut sbuf = self.alloc_cmd_sbuffer(
            size_of::<GspMsgHeader>() + size_of::<GspRpcHeader>() + cmd.size() as usize,
        )?;
        let msg_header_slice = unsafe {
            core::slice::from_raw_parts(
                &msg_header as *const GspMsgHeader as *const u8,
                size_of::<GspMsgHeader>(),
            )
        };
        let rpc_slice = unsafe {
            core::slice::from_raw_parts(
                &rpc as *const GspRpcHeader as *const u8,
                size_of::<GspRpcHeader>(),
            )
        };

        let mut sbuf_iter = sbuf.iter_mut();
        sbuf_iter.write_slice(msg_header_slice)?;
        sbuf_iter.write_slice(rpc_slice)?;
        cmd.copy_to_sbuf(&mut sbuf_iter)?;

        msg_header.checksum = 0;
        let total_size = sbuf.total_bytes;
        msg_header.elem_count = total_size.div_ceil(GSP_PAGE_SIZE) as u32;

        // Calculate checksum over the entire message
        msg_header.checksum = GspCmdq::calculate_checksum(&sbuf);

        // Re-write the message header with the updated element count and checksum
        sbuf.write(0, msg_header_slice)?;

        let mut wptr = self.cpu_wptr().unwrap() as u32;
        wptr += msg_header.elem_count as u32;
        wptr %= 0x3f;

        // TODO: Figure out Rust barriers
        unsafe {
            asm!("sfence";);
            dma_write!(self.gsp_mem[0].cpuq.tx.write_ptr = wptr)?;
            asm!("mfence";);
        };

        self.bar.try_access_with(|b| {
            NV_PGSP_QUEUE_HEAD::default().set_address(0 as u32).write(b);
        });

        Ok(())
    }

    fn receive<A: GspMessageElement>(self: &mut Self, function: u32) -> Result<A> {
        let header_size = (size_of::<GspMsgHeader>() + size_of::<GspRpcHeader>()) as u32;

        // Used pages contains the total number of pages available to consume
        let used_pages = self.get_used_rx_pages();
        if used_pages < header_size.div_ceil(GSP_PAGE_SIZE as u32) {
            return Err(EAGAIN);
        }

        let rptr = self.cpu_rptr().unwrap();

        // Remaining number of bytes left before we have to wrap
        let remaining = if rptr + used_pages > self.msg_count {
            (self.msg_count - rptr) << GSP_PAGE_SHIFT
        } else {
            used_pages << GSP_PAGE_SHIFT
        };

        let ptr = unsafe {
            core::ptr::addr_of_mut!((*self.gsp_mem.start_ptr_mut()).gspq.msgq[rptr as usize])
        };
        let msg_slice =
            unsafe { core::slice::from_raw_parts_mut(ptr as *mut u8, remaining as usize) };

        // TODO: Validating the checksum will read this
        let _msg = GspMsgHeader::new_from_slice(&msg_slice[0..size_of::<GspMsgHeader>()])?;
        let mut rpc = GspRpcHeader::new_from_slice(
            &msg_slice
                [size_of::<GspMsgHeader>()..size_of::<GspMsgHeader>() + size_of::<GspRpcHeader>()],
        )?;

        // rpc.length includes the size of the GspRpcHeader. Remove it to make
        // the rest of the code a bit easier to follow.
        rpc.length -= size_of::<GspRpcHeader>() as u32;

        // Not all pages of the message have made it to the queue so bail and let the caller retry.
        if used_pages << GSP_PAGE_SHIFT < header_size + rpc.length {
            return Err(EAGAIN);
        }

        let sbuf = if rpc.length + header_size < remaining {
            SBuffer::new((
                &mut msg_slice[(header_size as usize)..(header_size + rpc.length) as usize],
                None
            ))?
        } else {
            let slice_1 =
                &mut msg_slice[(header_size as usize)..(header_size + remaining) as usize];
            let ptr =
                unsafe { core::ptr::addr_of_mut!((*self.gsp_mem.start_ptr_mut()).gspq.msgq[0]) };
            let slice_2 = unsafe {
                core::slice::from_raw_parts_mut(ptr as *mut u8, rpc.length as usize - slice_1.len())
            };
            SBuffer::new((slice_1, Some(slice_2)))?
        };

        let result = if rpc.function == function {
            Ok(A::new_from_sbuf(&sbuf)?)
        } else {
            Err(ERANGE)
        };

        let mut rptr = self.cpu_rptr()?;
        rptr = rptr + (header_size + rpc.length).div_ceil(GSP_PAGE_SIZE as u32);
        rptr %= 0x3f;

        // TODO: Figure out Rust barriers
        unsafe {
            asm!("mfence";);
            dma_write!(self.gsp_mem[0].cpuq.rx.read_ptr = rptr)?;
        };

        result
    }

    /// Wait to receive a message matching `function`. If a different message is
    /// in the queue this will return `Err(ERANGE)`.
    fn receive_wait<R: GspMessageElement>(&mut self, timeout: Delta, function: u32) -> Result<R> {
        wait_on_result(timeout, || match self.receive::<R>(function) {
            Ok(x) => Some(Ok(x)),
            Err(EAGAIN) => None,
            Err(e) => Some(Err(e)),
        })
    }

    /// Same as the `receive_wait()` method but will consume and ingnore
    /// unexpected messages. Ie. messages with a different function to the passed
    /// `function` parameter.
    fn receive_wait_ignore<R: GspMessageElement>(
        &mut self,
        timeout: Delta,
        function: u32,
    ) -> Result<R> {
        wait_on_result(timeout, || match self.receive::<R>(function) {
            Ok(x) => Some(Ok(x)),
            Err(EAGAIN) => None,
            Err(ERANGE) => None,
            Err(e) => Some(Err(e)),
        })
    }

    pub(crate) fn run_sequencer(self: &mut Self, timeout: Delta) -> Result {
        let seq_info = self.receive_wait::<GspSequencerInfo>(
            timeout,
            fw::NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER,
        )?;
        self.bar.try_access_with(|bar| {
            match sequencer::GspSequencer::new(
                seq_info,
                bar,
                self.sec2_falcon,
                self.gsp_falcon,
                self.libos_dma_handle,
                self.fw,
            ) {
                Ok(sequencer) => {
                    if let Err(e) = sequencer.run() {
                        pr_info!("Error running CPU sequencer: {:?}\n", e);
                    }
                }
                Err(e) => {
                    pr_info!("Error creating CPU sequencer: {:?}\n", e);
                }
            }
        });

        Ok(())
    }

    pub(crate) fn gsp_init_done(&mut self, timeout: Delta) -> Result {
        self.receive_wait_ignore::<EmptyCmd>(timeout, fw::NV_VGPU_MSG_EVENT_GSP_INIT_DONE)
            .map(|_| ())
    }

    pub(crate) fn get_gsp_info(&mut self) -> Result<GspStaticConfigInfo> {
        self.send(
            fw::NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO,
            &EmptyCmd {
                size: size_of::<fw::GspStaticConfigInfo_t>(),
            },
        )?;
        self.receive_wait::<GspStaticConfigInfo>(
            Delta::from_secs(5),
            fw::NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO,
        )
    }
}

struct EmptyCmd {
    size: usize,
}

impl GspMessageElement for EmptyCmd {
    fn size(&self) -> usize {
        self.size
    }

    fn copy_to_sbuf(&self, sbuf: &mut SBufferIteratorMut<'_, '_>) -> Result {
        for _i in 0..self.size {
            sbuf.write_byte(0)?;
        }

        Ok(())
    }

    fn new_from_sbuf(sbuf: &SBuffer<'_>) -> Result<Self> {
        Ok(Self {
            size: sbuf.total_bytes,
        })
    }
}

pub(crate) fn build_wpr_meta(
    dev: &device::Device<device::Bound>,
    fw: &Firmware,
    fb_layout: &FbLayout,
) -> Result<CoherentAllocation<fw::GspFwWprMeta>> {
    let wpr_meta =
        CoherentAllocation::<fw::GspFwWprMeta>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;
    dma_write!(wpr_meta[0].magic = fw::GSP_FW_WPR_META_MAGIC as u64)?;
    dma_write!(wpr_meta[0].revision = fw::GSP_FW_WPR_META_REVISION as u64)?;
    dma_write!(wpr_meta[0].sysmemAddrOfRadix3Elf = fw.gsp.lvl0_dma_handle() as u64)?;
    dma_write!(wpr_meta[0].sizeOfRadix3Elf = fw.gsp.size() as u64)?;
    dma_write!(wpr_meta[0].sysmemAddrOfBootloader = fw.bootloader.ucode.dma_handle())?;
    dma_write!(wpr_meta[0].sizeOfBootloader = fw.bootloader.ucode.size() as u64)?;
    dma_write!(wpr_meta[0].bootloaderCodeOffset = fw.bootloader.code_offset as u64)?;
    dma_write!(wpr_meta[0].bootloaderDataOffset = fw.bootloader.data_offset as u64)?;
    dma_write!(wpr_meta[0].bootloaderManifestOffset = fw.bootloader.manifest_offset as u64)?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_1
            .__bindgen_anon_1
            .sysmemAddrOfSignature = fw.gsp_sigs.dma_handle() as u64
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_1
            .__bindgen_anon_1
            .sizeOfSignature = fw.gsp_sigs.size() as u64
    )?;
    dma_write!(wpr_meta[0].gspFwRsvdStart = fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].nonWprHeapOffset = fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].nonWprHeapSize = fb_layout.heap.end - fb_layout.heap.start)?;
    dma_write!(wpr_meta[0].gspFwWprStart = fb_layout.wpr2.start)?;
    dma_write!(wpr_meta[0].gspFwHeapOffset = fb_layout.wpr2_heap.start)?;
    dma_write!(wpr_meta[0].gspFwHeapSize = fb_layout.wpr2_heap.end - fb_layout.wpr2_heap.start)?;
    dma_write!(wpr_meta[0].gspFwOffset = fb_layout.elf.start)?;
    dma_write!(wpr_meta[0].bootBinOffset = fb_layout.boot.start)?;
    dma_write!(wpr_meta[0].frtsOffset = fb_layout.frts.start)?;
    dma_write!(wpr_meta[0].frtsSize = fb_layout.frts.end - fb_layout.frts.start)?;
    dma_write!(wpr_meta[0].gspFwWprEnd = fb_layout.vga_workspace.start & !(0x20000 - 1))?;
    dma_write!(wpr_meta[0].gspFwHeapVfPartitionCount = fb_layout.vf_partition_count)?;
    dma_write!(wpr_meta[0].fbSize = fb_layout.fb.end - fb_layout.fb.start)?;
    dma_write!(wpr_meta[0].vgaWorkspaceOffset = fb_layout.vga_workspace.start)?;
    dma_write!(
        wpr_meta[0].vgaWorkspaceSize = fb_layout.vga_workspace.end - fb_layout.vga_workspace.start
    )?;
    dma_write!(wpr_meta[0].bootCount = 0)?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcAddr = 0
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcRequestOffset = 0
    )?;
    dma_write!(
        wpr_meta[0]
            .__bindgen_anon_2
            .__bindgen_anon_1
            .partitionRpcReplyOffset = 0
    )?;
    dma_write!(wpr_meta[0].verified = 0)?;

    Ok(wpr_meta)
}

#[allow(unused)]
pub(crate) struct GspMemObjects<'a> {
    pub libos: DmaObject,
    pub loginit: DmaObject,
    pub logintr: DmaObject,
    pub logrm: DmaObject,
    pub rmargs: CoherentAllocation<fw::GSP_ARGUMENTS_CACHED>,
    pub cmdq: GspCmdq<'a>,
}

/// Generates the `ID8` identifier required for some GSP objects.
fn id8(name: &str) -> u64 {
    let mut bytes = [0u8; core::mem::size_of::<u64>()];

    for (c, b) in name.bytes().rev().zip(&mut bytes) {
        *b = c;
    }

    u64::from_ne_bytes(bytes)
}

/// Creates a self-mapping page table for `obj` at its beginning.
fn create_pte_array(obj: &mut DmaObject) {
    let num_pages = obj.size().div_ceil(GSP_PAGE_SIZE);
    let handle = obj.dma_handle();

    let ptes = unsafe {
        let ptr = obj
            .start_ptr_mut()
            .add(core::mem::size_of::<u64>())
            .cast::<u64>();
        core::slice::from_raw_parts_mut(ptr, num_pages)
    };

    for (i, pte) in ptes.iter_mut().enumerate() {
        *pte = handle as u64 + ((i as u64) << GSP_PAGE_SHIFT);
    }
}

/// Creates a new `DmaObject` with `name` of `size`, and register it into the `libos` object at
/// argument position `libos_arg_nr`.
fn create_dma_object(
    dev: &device::Device<device::Bound>,
    name: &'static str,
    size: usize,
    libos: &mut DmaObject,
    libos_arg_nr: usize,
) -> Result<DmaObject> {
    let mut obj = DmaObject::new(dev, size)?;
    create_pte_array(&mut obj);

    let arg_offset = libos_arg_nr * size_of::<fw::LibosMemoryRegionInitArgument>();
    let libos_start_ptr = unsafe { libos.start_ptr_mut().add(arg_offset) };

    let libos_mem_init_args = fw::LibosMemoryRegionInitArgument {
        id8: id8(name),
        pa: obj.dma_handle(),
        size: obj.size() as u64,
        kind: fw::LibosMemoryRegionKind_LIBOS_MEMORY_REGION_CONTIGUOUS as u8,
        loc: fw::LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_SYSMEM as u8,
    };
    unsafe {
        core::ptr::copy_nonoverlapping(
            &libos_mem_init_args as *const fw::LibosMemoryRegionInitArgument,
            libos_start_ptr as *mut fw::LibosMemoryRegionInitArgument,
            1,
        );
    };

    Ok(obj)
}

const GSP_REGISTRY_NUM_ENTRIES: usize = 2;
struct RegistryEntry {
    key: &'static str,
    value: u32,
}

struct RegistryTable {
    entries: [RegistryEntry; GSP_REGISTRY_NUM_ENTRIES],
}

impl GspMessageElement for RegistryTable {
    fn copy_to_sbuf(&self, sbuf: &mut SBufferIteratorMut<'_, '_>) -> Result {
        let total_size = self.size();
        let align = core::mem::align_of::<fw::PACKED_REGISTRY_TABLE>();
        let layout = Layout::from_size_align(total_size, align)
            .map_err(|_| ENOMEM)
            .unwrap();
        let cmd_slice = unsafe {
            // Use the kernel allocator which respects alignment
            let allocation = Kmalloc::alloc(layout, GFP_KERNEL | __GFP_ZERO).unwrap();
            let ptr = allocation.as_ptr() as *mut u8;

            // Verify alignment (debug only)
            debug_assert_eq!(ptr as usize % align, 0);

            // Serialize the data into the allocated memory
            let table = ptr as *mut fw::PACKED_REGISTRY_TABLE;
            let mut table_data = ptr.add(
                size_of::<fw::PACKED_REGISTRY_TABLE>()
                    + GSP_REGISTRY_NUM_ENTRIES * size_of::<fw::PACKED_REGISTRY_ENTRY>(),
            );

            (*table).numEntries = GSP_REGISTRY_NUM_ENTRIES as u32;
            (*table).size = total_size as u32;

            for i in 0..GSP_REGISTRY_NUM_ENTRIES {
                let entry_ptr = ptr.add(
                    size_of::<fw::PACKED_REGISTRY_TABLE>()
                        + i * size_of::<fw::PACKED_REGISTRY_ENTRY>(),
                ) as *mut fw::PACKED_REGISTRY_ENTRY;

                (*entry_ptr).nameOffset = table_data.offset_from(table as *const u8) as u32;
                (*entry_ptr).type_ = fw::REGISTRY_TABLE_ENTRY_TYPE_DWORD as u8;
                (*entry_ptr).data = self.entries[i].value;
                (*entry_ptr).length = 0;

                // Copy the key string to table_data and null terminate it
                let key_bytes = self.entries[i].key.as_bytes();
                core::ptr::copy_nonoverlapping(key_bytes.as_ptr(), table_data, key_bytes.len());
                table_data = table_data.add(key_bytes.len());
                *table_data = 0; // Add null terminator
                table_data = table_data.add(1); // Move past null terminator
            }

            core::slice::from_raw_parts(ptr as *const u8, layout.size())
        };

        sbuf.write_slice(cmd_slice)?;

        // Free the allocated memory by converting slice back to pointer.
        unsafe {
            use core::ptr::NonNull;
            let ptr = cmd_slice.as_ptr() as *mut u8;
            let ptr_nn = NonNull::new_unchecked(ptr);
            Kmalloc::free(ptr_nn, layout);
        }

        Ok(())
    }

    fn size(&self) -> usize {
        let mut key_size = 0;
        for i in 0..GSP_REGISTRY_NUM_ENTRIES {
            key_size += self.entries[i].key.len() + 1; // +1 for NULL terminator
        }
        size_of::<fw::PACKED_REGISTRY_TABLE>()
            + GSP_REGISTRY_NUM_ENTRIES * size_of::<fw::PACKED_REGISTRY_ENTRY>()
            + key_size
    }
}

fn build_registry<'a>(cmdq: &mut GspCmdq<'a>) {
    let registry = RegistryTable {
        entries: [
            RegistryEntry {
                key: "RMSecBusResetEnable",
                value: 1,
            },
            RegistryEntry {
                key: "RMForcePcieConfigSave",
                value: 1,
            },
        ],
    };

    cmdq.send(fw::NV_VGPU_MSG_FUNCTION_SET_REGISTRY, &registry)
        .unwrap();
}

impl GspMessageElement for fw::GspSystemInfo {}

fn set_system_info<'a>(dev: &pci::Device<device::Bound>, cmdq: &mut GspCmdq<'a>) -> Result {
    let mut info = unsafe { MaybeUninit::<fw::GspSystemInfo>::zeroed().assume_init() };

    info.gpuPhysAddr = dev.resource_start(0)?;
    info.gpuPhysFbAddr = dev.resource_start(1)?;
    info.gpuPhysInstAddr = dev.resource_start(3)?;
    info.nvDomainBusDeviceFunc = dev.dev_id() as u64;

    // Using TASK_SIZE in r535_gsp_rpc_set_system_info() seems wrong because
    // TASK_SIZE is per-task. That's probably a design issue in GSP-RM though.
    info.maxUserVa = (1 << 47) - 4096;
    info.pciConfigMirrorBase = 0x088000;
    info.pciConfigMirrorSize = 0x001000;

    info.PCIDeviceID = ((dev.device_id() as u32) << 16) | dev.vendor_id() as u32;
    info.PCISubDeviceID =
        ((dev.subsystem_device_id() as u32) << 16) | dev.subsystem_vendor_id() as u32;
    info.PCIRevisionID = dev.revision_id() as u32;
    info.bIsPrimary = 0;
    info.bPreserveVideoMemoryAllocations = 0;

    cmdq.send(fw::NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO, &info)?;
    Ok(())
}

fn create_coherent_dma_object<A: AsBytes + FromBytes>(
    dev: &device::Device<device::Bound>,
    name: &'static str,
    libos: &mut DmaObject,
    libos_arg_nr: usize,
) -> Result<CoherentAllocation<A>> {
    let obj = CoherentAllocation::<A>::alloc_coherent(dev, 1, GFP_KERNEL | __GFP_ZERO)?;

    let arg_offset = libos_arg_nr * size_of::<fw::LibosMemoryRegionInitArgument>();
    let libos_start_ptr = unsafe { libos.start_ptr_mut().add(arg_offset) };

    let libos_mem_init_args = fw::LibosMemoryRegionInitArgument {
        id8: id8(name),
        pa: obj.dma_handle(),
        size: obj.size() as u64,
        kind: fw::LibosMemoryRegionKind_LIBOS_MEMORY_REGION_CONTIGUOUS as u8,
        loc: fw::LibosMemoryRegionLoc_LIBOS_MEMORY_REGION_LOC_SYSMEM as u8,
    };
    unsafe {
        core::ptr::copy_nonoverlapping(
            &libos_mem_init_args as *const fw::LibosMemoryRegionInitArgument,
            libos_start_ptr as *mut fw::LibosMemoryRegionInitArgument,
            1,
        );
    };

    Ok(obj)
}

impl<'a> GspMemObjects<'a> {
    pub(crate) fn new(
        pdev: &pci::Device<device::Bound>,
        bar: &'a Devres<Bar0>,
        gsp_falcon: &'a Falcon<Gsp>,
        sec2_falcon: &'a Falcon<Sec2>,
        fw: &'a Firmware,
    ) -> Result<Self> {
        let dev = pdev.as_ref();
        let mut libos = DmaObject::new(dev, GSP_PAGE_SIZE)?;
        let loginit = create_dma_object(dev, "LOGINIT", 0x10000, &mut libos, 0)?;
        let logintr = create_dma_object(dev, "LOGINTR", 0x10000, &mut libos, 1)?;
        let logrm = create_dma_object(dev, "LOGRM", 0x10000, &mut libos, 2)?;

        // Creates its own PTE array
        let mut cmdq = GspCmdq::new(dev, bar, gsp_falcon, sec2_falcon, libos.dma_handle(), fw)?;
        let rmargs =
            create_coherent_dma_object::<fw::GSP_ARGUMENTS_CACHED>(dev, "RMARGS", &mut libos, 3)?;
        dma_write!(
            rmargs[0].messageQueueInitArguments.sharedMemPhysAddr = cmdq.gsp_mem.dma_handle()
        )?;
        dma_write!(rmargs[0].messageQueueInitArguments.pageTableEntryCount = cmdq.nr_ptes)?;
        dma_write!(rmargs[0].messageQueueInitArguments.cmdQueueOffset = 0x1000)?;
        dma_write!(rmargs[0].messageQueueInitArguments.statQueueOffset = 0x41000)?;
        dma_write!(rmargs[0].srInitArguments.oldLevel = 0)?;
        dma_write!(rmargs[0].srInitArguments.flags = 0)?;
        dma_write!(rmargs[0].srInitArguments.bInPMTransition = 0)?;
        dma_write!(rmargs[0].bDmemStack = 1)?;

        set_system_info(pdev, &mut cmdq)?;
        build_registry(&mut cmdq);

        Ok(GspMemObjects {
            libos,
            loginit,
            logintr,
            logrm,
            rmargs,
            cmdq,
        })
    }
}
