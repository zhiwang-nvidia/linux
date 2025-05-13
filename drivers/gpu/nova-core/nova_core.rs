// SPDX-License-Identifier: GPL-2.0

//! Nova Core GPU Driver

#[macro_use]
mod macros {
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
}

mod dma;
mod driver;
mod falcon;
mod fb;
mod firmware;
mod gfw;
mod gpu;
mod gsp;
mod nvfw;
mod regs;
mod sbuffer;
mod util;
mod vbios;

pub(crate) const MODULE_NAME: &kernel::str::CStr = <LocalModule as kernel::ModuleMetadata>::NAME;

kernel::module_pci_driver! {
    type: driver::NovaCore,
    name: "NovaCore",
    author: "Danilo Krummrich",
    description: "Nova Core GPU driver",
    license: "GPL v2",
    firmware: [],
}

kernel::module_firmware!(firmware::ModInfoBuilder);
