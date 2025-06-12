// SPDX-License-Identifier: GPL-2.0

//! Bindings.
//!
//! Imports the generated bindings by `bindgen`.
//!
//! This crate may not be directly used. If you need a kernel C API that is
//! not ported or wrapped in the `kernel` crate, then do so first instead of
//! using this crate.

// See <https://github.com/rust-lang/rust-bindgen/issues/1651>.
#![cfg_attr(test, allow(deref_nullptr))]
#![cfg_attr(test, allow(unaligned_references))]
#![cfg_attr(test, allow(unsafe_op_in_unsafe_fn))]
#![allow(
    dead_code,
    clippy::all,
    missing_docs,
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    improper_ctypes,
    unreachable_pub,
    unsafe_op_in_unsafe_fn
)]

#[allow(clippy::undocumented_unsafe_blocks)]
use kernel::ffi;
include!("r570_144_bindings.rs");
