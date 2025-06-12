// SPDX-License-Identifier: GPL-2.0

//! Numerical and binary utilities for primitive types.

use crate::build_assert;
use core::fmt::Debug;
use core::hash::Hash;

/// An unsigned integer which is guaranteed to be a power of 2.
///
/// # Invariants
///
/// The stored value is guaranteed to be a power of two.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(transparent)]
pub struct PowerOfTwo<T>(T);

macro_rules! power_of_two_impl {
    ($($t:ty),+) => {
        $(
            impl PowerOfTwo<$t> {
                /// Validates that `v` is a power of two at build-time, and returns it wrapped into
                /// [`PowerOfTwo`].
                ///
                /// A build error is triggered if `v` cannot be asserted to be a power of two.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!("let v = PowerOfTwo::<", stringify!($t), ">::new(16);")]
                /// assert_eq!(v.value(), 16);
                /// ```
                #[inline(always)]
                pub const fn new(v: $t) -> Self {
                    build_assert!(v.count_ones() == 1);
                    Self(v)
                }

                /// Validates that `v` is a power of two at runtime, and returns it wrapped into
                /// [`PowerOfTwo`].
                ///
                /// [`None`] is returned if `v` was not a power of two.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::try_new(16), Some(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(16)));"
                )]
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::try_new(15), None);"
                )]
                /// ```
                #[inline(always)]
                pub const fn try_new(v: $t) -> Option<Self> {
                    match v.count_ones() {
                        1 => Some(Self(v)),
                        _ => None,
                    }
                }

                /// Returns the value of this instance.
                ///
                /// It is guaranteed to be a power of two.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!("let v = PowerOfTwo::<", stringify!($t), ">::new(16);")]
                /// assert_eq!(v.value(), 16);
                /// ```
                #[inline(always)]
                pub const fn value(self) -> $t {
                    self.0
                }

                /// Returns the mask corresponding to `self.value() - 1`.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!("let v = PowerOfTwo::<", stringify!($t), ">::new(0x10);")]
                /// assert_eq!(v.mask(), 0xf);
                /// ```
                #[inline(always)]
                pub const fn mask(self) -> $t {
                    self.0.wrapping_sub(1)
                }

                /// Aligns `self` down to `alignment`.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(0x10).align_down(0x2f), 0x20);"
                )]
                /// ```
                #[inline(always)]
                pub const fn align_down(self, value: $t) -> $t {
                    value & !self.mask()
                }

                /// Aligns `value` up to `self`.
                ///
                /// Wraps around to `0` if the requested alignment pushes the result above the
                /// type's limits.
                ///
                /// # Examples
                ///
                /// ```
                /// use kernel::num::PowerOfTwo;
                ///
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(0x10).align_up(0x4f), 0x50);"
                )]
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(0x10).align_up(0x40), 0x40);"
                )]
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(0x10).align_up(0x0), 0x0);"
                )]
                #[doc = concat!(
                    "assert_eq!(PowerOfTwo::<",
                    stringify!($t),
                    ">::new(0x10).align_up(",
                    stringify!($t), "::MAX), 0x0);"
                )]
                /// ```
                #[inline(always)]
                pub const fn align_up(self, value: $t) -> $t {
                    self.align_down(value.wrapping_add(self.mask()))
                }
            }
        )+
    };
}

power_of_two_impl!(usize, u8, u16, u32, u64, u128);

macro_rules! impl_last_set_bit {
    ($($t:ty),+) => {
        $(
            ::kernel::macros::paste! {
            /// Last Set Bit: return the 1-based index of the last (i.e. most significant) set bit
            /// in `v`.
            ///
            /// Equivalent to the C `fls` function.
            ///
            /// # Examples
            ///
            /// ```
            #[doc = concat!("use kernel::num::last_set_bit_", stringify!($t), ";")]
            ///
            #[doc = concat!("assert_eq!(last_set_bit_", stringify!($t), "(0x0), 0);")]
            #[doc = concat!("assert_eq!(last_set_bit_", stringify!($t), "(0x1), 1);")]
            #[doc = concat!("assert_eq!(last_set_bit_", stringify!($t), "(0x10), 5);")]
            #[doc = concat!("assert_eq!(last_set_bit_", stringify!($t), "(0x1f), 5);")]
            #[doc = concat!(
                "assert_eq!(last_set_bit_",
                stringify!($t),
                "(",
                stringify!($t),
                "::MAX), ",
                stringify!($t), "::BITS);"
            )]
            /// ```
            #[inline(always)]
            pub const fn [<last_set_bit_ $t>](v: $t) -> u32 {
                $t::BITS - v.leading_zeros()
            }
            }
        )+
    };
}

impl_last_set_bit!(usize, u8, u16, u32, u64, u128);
