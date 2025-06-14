// SPDX-License-Identifier: GPL-2.0

//! Contains structures and functions dedicated to the parsing, building and patching of firmwares
//! to be loaded into a given execution unit.

use core::marker::PhantomData;

use kernel::bindings;
use kernel::device;
use kernel::firmware;
use kernel::prelude::*;
use kernel::str::CString;
use radix3::RadixFirmware;
use riscv::RiscvFirmware;
use sec2::Sec2Firmware;

use crate::dma::DmaObject;
use crate::driver::Bar0;
use crate::falcon::FalconFirmware;
use crate::falcon::{sec2::Sec2, Falcon};
use crate::gpu;
use crate::gpu::Chipset;

pub(crate) mod fwsec;
pub(crate) mod radix3;
pub(crate) mod riscv;
pub(crate) mod sec2;

pub(crate) const FIRMWARE_VERSION: &str = "570.144";

fn elf_section<'a, 'b>(elf: &'a [u8], name: &'b str) -> Option<&'a [u8]> {
    let hdr = elf
        .get(0..size_of::<bindings::elf64_hdr>())
        .map(|slice| slice.as_ptr())
        .filter(|ptr| ptr.align_offset(align_of::<bindings::elf64_hdr>()) == 0)
        // SAFETY:
        // * `get` guarantees that the slice is within the bounds of `elf` and of the size of
        //   `elf64_hdr`.
        // * We checked that `ptr` had the correct alignment for `elf64_hdr`.
        .map(|ptr| unsafe { &*ptr.cast::<bindings::elf64_hdr>() })?;

    let shdr_off = hdr.e_shoff as usize;
    let shdr_num = hdr.e_shnum as usize;
    let shdr = elf
        .get(shdr_off..shdr_off + size_of::<bindings::elf64_shdr>() * shdr_num)
        .map(|slice| slice.as_ptr())
        .filter(|ptr| ptr.align_offset(align_of::<bindings::elf64_shdr>()) == 0)
        // SAFETY:
        // * `get` guarantees that the slice is within the bounds of `elf` and of size
        //   `elf64_shdr * shdr_num`.
        // * We checked that `ptr` had the correct alignment for `elf64_shdr`.
        .map(|ptr| unsafe {
            core::slice::from_raw_parts(ptr.cast::<bindings::elf64_shdr>(), shdr_num)
        })?;

    // Get the strings table.
    let strhdr = shdr.get(hdr.e_shstrndx as usize)?;

    // Find the section which name matches `name` and return it.
    shdr.iter()
        .find(|sh| {
            let name_idx = strhdr.sh_offset as usize + sh.sh_name as usize;

            // Get the start of the name.
            elf.get(name_idx..)
                // Stop at the first `0`.
                .and_then(|nstr| nstr.get(0..=nstr.iter().position(|b| *b == 0)?))
                // Convert into CStr. This should never fail because of the line above.
                .and_then(|nstr| CStr::from_bytes_with_nul(nstr).ok())
                // Convert into str.
                .and_then(|c_str| c_str.to_str().ok())
                // Check that the name matches.
                .map(|str| str == name)
                .unwrap_or(false)
        })
        // Return the slice containing the section.
        .and_then(|sh| {
            let start = sh.sh_offset as usize;

            elf.get(start..start + sh.sh_size as usize)
        })
}

/// Structure encapsulating the firmware blobs required for the GPU to operate.
#[expect(dead_code)]
pub(crate) struct Firmware {
    pub booter_load: Sec2Firmware,
    pub booter_unload: Sec2Firmware,
    pub bootloader: RiscvFirmware,
    pub gsp: RadixFirmware,
    pub gsp_sigs: DmaObject,
    pub gsp_desc: RmRiscvUCodeDesc,
}

impl Firmware {
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        sec2: &Falcon<Sec2>,
        bar: &Bar0,
        chipset: Chipset,
        ver: &str,
    ) -> Result<Firmware> {
        let mut chip_name = CString::try_from_fmt(fmt!("{}", chipset))?;
        chip_name.make_ascii_lowercase();

        let request = |name_| {
            CString::try_from_fmt(fmt!("nvidia/{}/gsp/{}-{}.bin", &*chip_name, name_, ver))
                .and_then(|path| firmware::Firmware::request(&path, dev))
        };

        let gsp_fw = request("gsp")?;

        let (gsp, gsp_desc) = {
            // Extract the .fwimage section for the GSP firmware
            let data = elf_section(gsp_fw.data(), ".fwimage").ok_or(EINVAL)?;

            let gsp = RadixFirmware::new(dev, ".fwimage", data)?;

            // Extract RISC-V ucode descriptor
            let hdr = data
                .get(0..size_of::<BinHdr>())
                .ok_or(EINVAL)
                .and_then(BinHdr::from_bytes)?;

            let offset = hdr.header_offset as usize;
            let desc = data
                .get(offset..offset + size_of::<RmRiscvUCodeDesc>())
                .ok_or(EINVAL)
                .and_then(RmRiscvUCodeDesc::from_bytes)?;

            (gsp, desc)
        };

        // TODO: make this a GPU-specific const.
        let gsp_sigs_section = ".fwsignature_ga10x";
        let gsp_sigs = elf_section(gsp_fw.data(), gsp_sigs_section)
            .ok_or(EINVAL)
            .and_then(|data| DmaObject::from_data(dev, data))?;

        Ok(Firmware {
            booter_load: request("booter_load")
                .and_then(|fw| Sec2Firmware::new(sec2, dev, bar, &fw))?,
            booter_unload: request("booter_unload")
                .and_then(|fw| Sec2Firmware::new(sec2, dev, bar, &fw))?,
            bootloader: request("bootloader").and_then(|fw| RiscvFirmware::new(dev, &fw))?,
            gsp,
            gsp_sigs,
            gsp_desc,
        })
    }
}

/// Structure used to describe some firmwares, notably FWSEC-FRTS.
#[repr(C)]
#[derive(Debug, Clone)]
pub(crate) struct FalconUCodeDescV3 {
    /// Header defined by `NV_BIT_FALCON_UCODE_DESC_HEADER_VDESC*` in OpenRM.
    hdr: u32,
    /// Stored size of the ucode after the header.
    stored_size: u32,
    /// Offset in `DMEM` at which the signature is expected to be found.
    pub(crate) pkc_data_offset: u32,
    /// Offset after the code segment at which the app headers are located.
    pub(crate) interface_offset: u32,
    /// Base address at which to load the code segment into `IMEM`.
    pub(crate) imem_phys_base: u32,
    /// Size in bytes of the code to copy into `IMEM`.
    pub(crate) imem_load_size: u32,
    /// Virtual `IMEM` address (i.e. `tag`) at which the code should start.
    pub(crate) imem_virt_base: u32,
    /// Base address at which to load the data segment into `DMEM`.
    pub(crate) dmem_phys_base: u32,
    /// Size in bytes of the data to copy into `DMEM`.
    pub(crate) dmem_load_size: u32,
    /// Mask of the falcon engines on which this firmware can run.
    pub(crate) engine_id_mask: u16,
    /// ID of the ucode used to infer a fuse register to validate the signature.
    pub(crate) ucode_id: u8,
    /// Number of signatures in this firmware.
    pub(crate) signature_count: u8,
    /// Versions of the signatures, used to infer a valid signature to use.
    pub(crate) signature_versions: u16,
    _reserved: u16,
}

impl FalconUCodeDescV3 {
    /// Returns the size in bytes of the header.
    pub(crate) fn size(&self) -> usize {
        const HDR_SIZE_SHIFT: u32 = 16;
        const HDR_SIZE_MASK: u32 = 0xffff0000;

        ((self.hdr & HDR_SIZE_MASK) >> HDR_SIZE_SHIFT) as usize
    }
}

/// Trait implemented by types defining the signed state of a firmware.
trait SignedState {}

/// Type indicating that the firmware must be signed before it can be used.
struct Unsigned;
impl SignedState for Unsigned {}

/// Type indicating that the firmware is signed and ready to be loaded.
struct Signed;
impl SignedState for Signed {}

/// A [`DmaObject`] containing a specific microcode ready to be loaded into a falcon.
///
/// This is module-local and meant for sub-modules to use internally.
///
/// After construction, a firmware is [`Unsigned`], and must generally be patched with a signature
/// before it can be loaded (with an exception for development hardware). The
/// [`Self::patch_signature`] and [`Self::no_patch_signature`] methods are used to transition the
/// firmware to its [`Signed`] state.
struct FirmwareDmaObject<F: FalconFirmware, S: SignedState>(DmaObject, PhantomData<(F, S)>);

/// Trait for signatures to be patched directly into a given firmware.
///
/// This is module-local and meant for sub-modules to use internally.
trait FirmwareSignature<F: FalconFirmware>: AsRef<[u8]> {}

impl<F: FalconFirmware> FirmwareDmaObject<F, Unsigned> {
    /// Patches the firmware at offset `sig_base_img` with `signature`.
    fn patch_signature<S: FirmwareSignature<F>>(
        mut self,
        signature: &S,
        sig_base_img: usize,
    ) -> Result<FirmwareDmaObject<F, Signed>> {
        let signature_bytes = signature.as_ref();
        if sig_base_img + signature_bytes.len() > self.0.size() {
            return Err(EINVAL);
        }

        // SAFETY: We are the only user of this object, so there cannot be any race.
        let dst = unsafe { self.0.start_ptr_mut().add(sig_base_img) };

        // SAFETY: `signature` and `dst` are valid, properly aligned, and do not overlap.
        unsafe {
            core::ptr::copy_nonoverlapping(signature_bytes.as_ptr(), dst, signature_bytes.len())
        };

        Ok(FirmwareDmaObject(self.0, PhantomData))
    }

    /// Mark the firmware as signed without patching it.
    ///
    /// This method is used to explicitly confirm that we do not need to sign the firmware, while
    /// allowing us to continue as if it was. This is typically only needed for development
    /// hardware.
    fn no_patch_signature(self) -> FirmwareDmaObject<F, Signed> {
        FirmwareDmaObject(self.0, PhantomData)
    }
}

// TODO: turn this into a local marker trait that is auto-implemented for structs implementing
// `FromBytes`.
macro_rules! impl_from_bytes {
    ($name:ty) => {
        impl $name {
            pub(crate) fn from_bytes(bytes: &[u8]) -> Result<Self> {
                let mut data: [u8; size_of::<Self>()] = bytes.try_into().map_err(|_| EINVAL)?;

                const U32_SIZE: usize = size_of::<u32>();
                data.chunks_exact_mut(U32_SIZE)
                    .map(|slice| <&mut [u8; U32_SIZE]>::try_from(slice).unwrap())
                    .for_each(|chunk| *chunk = u32::from_le_bytes(*chunk).to_ne_bytes());

                // SAFETY: the `FromBytes` implementation guarantees that any byte stream is valid
                // for `Self`.
                Ok(unsafe { core::mem::transmute::<[u8; size_of::<Self>()], Self>(data) })
            }
        }
    };
}

#[repr(C)]
#[derive(Debug)]
struct BinHdr {
    pub bin_magic: u32,
    pub bin_ver: u32,
    pub bin_size: u32,
    pub header_offset: u32,
    pub data_offset: u32,
    pub data_size: u32,
}
impl_from_bytes!(BinHdr);

#[repr(C)]
#[derive(Debug)]
struct HsHeaderV2 {
    pub sig_prod_offset: u32,
    pub sig_prod_size: u32,
    pub patch_loc: u32,
    pub patch_sig: u32,
    pub meta_data_offset: u32,
    pub meta_data_size: u32,
    pub num_sig: u32,
    pub header_offset: u32,
    pub header_size: u32,
}
impl_from_bytes!(HsHeaderV2);

#[repr(C)]
#[derive(Debug)]
struct HsLoadHeaderV2 {
    pub os_code_offset: u32,
    pub os_code_size: u32,
    pub os_data_offset: u32,
    pub os_data_size: u32,
    pub num_apps: u32,
}
impl_from_bytes!(HsLoadHeaderV2);

#[repr(C)]
#[derive(Debug)]
struct HsLoadHeaderV2App {
    pub offset: u32,
    pub len: u32,
}
impl_from_bytes!(HsLoadHeaderV2App);

#[repr(C)]
#[derive(Debug)]
pub(crate) struct RmRiscvUCodeDesc {
    version: u32,
    bootloader_offset: u32,
    bootloader_size: u32,
    bootloader_param_offset: u32,
    bootloader_param_size: u32,
    riscv_elf_offset: u32,
    riscv_elf_size: u32,
    app_version: u32,
    manifest_offset: u32,
    manifest_size: u32,
    monitor_data_offset: u32,
    monitor_data_size: u32,
    monitor_code_offset: u32,
    monitor_code_size: u32,
}
impl_from_bytes!(RmRiscvUCodeDesc);

pub(crate) struct ModInfoBuilder<const N: usize>(firmware::ModInfoBuilder<N>);

impl<const N: usize> ModInfoBuilder<N> {
    const fn make_entry_file(self, chipset: &str, fw: &str) -> Self {
        ModInfoBuilder(
            self.0
                .new_entry()
                .push("nvidia/")
                .push(chipset)
                .push("/gsp/")
                .push(fw)
                .push("-")
                .push(FIRMWARE_VERSION)
                .push(".bin"),
        )
    }

    const fn make_entry_chipset(self, chipset: &str) -> Self {
        self.make_entry_file(chipset, "booter_load")
            .make_entry_file(chipset, "booter_unload")
            .make_entry_file(chipset, "bootloader")
            .make_entry_file(chipset, "gsp")
    }

    pub(crate) const fn create(
        module_name: &'static kernel::str::CStr,
    ) -> firmware::ModInfoBuilder<N> {
        let mut this = Self(firmware::ModInfoBuilder::new(module_name));
        let mut i = 0;

        while i < gpu::Chipset::NAMES.len() {
            this = this.make_entry_chipset(gpu::Chipset::NAMES[i]);
            i += 1;
        }

        this.0
    }
}
