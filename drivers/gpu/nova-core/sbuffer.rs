// SPDX-License-Identifier: GPL-2.0

use kernel::alloc::{flags::GFP_KERNEL, KVec};
use kernel::error::code::*;
use kernel::prelude::*;
use kernel::str::CString;

/// A buffer abstraction for discontiguous byte slices.
///
/// This allows you to treat multiple non-contiguous `&mut [u8]` slices
/// as a single stream-like read/write buffer.
///
/// Example:
///
/// let mut buf1 = [0u8; 3];
/// let mut buf2 = [0u8; 5];
/// let mut sbuffer = SBuffer::new((&mut buf1[..], Some(&mut buf2[..])));
///
/// let data = b"hellowo";
/// let result = sbuffer.write(0, data);
///

pub(crate) struct SBuffer<'a> {
    /// First slice (always present) and optional second slice
    slices: (&'a mut [u8], Option<&'a mut [u8]>),
    pub total_bytes: usize,
}

impl<'a> SBuffer<'a> {
    /// Create SBuffer from up to 2 slices of `&'a mut [u8]`. First slice is required
    pub(crate) fn new(slices: (&'a mut [u8], Option<&'a mut [u8]>)) -> Result<Self>
    {
        let first_slice = slices.0;
        let second_slice = slices.1;
        let total_bytes = first_slice.len() + second_slice.as_ref().map_or(0, |s| s.len());

        Ok(SBuffer {
            slices: (first_slice, second_slice),
            total_bytes,
        })
    }

    /// Get the total number of slices in the SBuffer
    fn len(&self) -> usize {
        if self.slices.1.is_some() { 2 } else { 1 }
    }

    /// Get immutable slice by index
    fn idx_to_slice(&self, idx: usize) -> Option<&[u8]> {
        match idx {
            0 => Some(&self.slices.0),
            1 => self.slices.1.as_ref().map(|v| &**v),
            _ => None,
        }
    }

    /// Get mutable slice by index
    fn idx_to_slice_mut(&mut self, idx: usize) -> Option<&mut [u8]> {
        match idx {
            0 => Some(&mut self.slices.0),
            1 => self.slices.1.as_mut().map(|v| &mut **v),
            _ => None,
        }
    }

    pub(crate) fn write(&mut self, offset: usize, mut src: &[u8]) -> Result {
        if src.len() > self.total_bytes - offset {
            return Err(EINVAL);
        }
        let (mut idx, mut offset) = self.get_pos_from_offset(offset)?;

        while !src.is_empty() && idx < self.len() {
            let current = match self.idx_to_slice_mut(idx) {
                Some(slice) => slice,
                None => break,
            };

            let remaining = &mut current[offset..];
            let n = remaining.len().min(src.len());
            remaining[..n].copy_from_slice(&src[..n]);
            src = &src[n..];
            idx += 1;
            offset = 0;
        }

        Ok(())
    }

    pub(crate) fn read(&self, offset: usize, mut dst: &mut [u8]) -> Result {
        if dst.len() > self.total_bytes - offset {
            return Err(EINVAL);
        }
        let (mut idx, mut offset) = self.get_pos_from_offset(offset)?;

        while !dst.is_empty() && idx < self.len() {
            let current = match self.idx_to_slice(idx) {
                Some(slice) => slice,
                None => break,
            };

            let remaining = &current[offset..];
            let n = remaining.len().min(dst.len());
            dst[..n].copy_from_slice(&remaining[..n]);
            dst = &mut dst[n..];
            idx += 1;
            offset = 0;
        }

        Ok(())
    }

    pub(crate) fn read_byte(&self, offset: usize) -> Result<u8> {
        let mut buf = [0u8];
        self.read(offset, &mut buf)?;
        Ok(buf[0])
    }

    #[allow(dead_code)]
    pub(crate) fn read_word(&self, offset: usize) -> Result<u16> {
        let mut buf = [0u8; 2];
        self.read(offset, &mut buf)?;
        Ok(u16::from_le_bytes(buf))
    }

    #[allow(dead_code)]
    pub(crate) fn read_dword(&self, offset: usize) -> Result<u32> {
        let mut buf = [0u8; 4];
        self.read(offset, &mut buf)?;
        Ok(u32::from_le_bytes(buf))
    }

    #[allow(dead_code)]
    pub(crate) fn read_bytes(&self, offset: usize, dst: &mut [u8]) -> Result {
        self.read(offset, dst)
    }

    #[allow(dead_code)]
    pub(crate) fn read_str(&self, offset: usize, len: usize) -> Result<CString> {
        let mut buf = KVec::<u8>::new();
        buf.extend_with(len, 0u8, GFP_KERNEL)?;
        self.read(offset, &mut buf)?;
        let string = core::str::from_utf8(&buf).map_err(|_| EINVAL)?;
        CString::try_from_fmt(fmt!("{}", string)).map_err(|_| ENOMEM)
    }

    pub(crate) fn read_kvec(&self, mut offset: usize) -> Result<KVec<u8>> {
        if offset > self.total_bytes {
            return Err(ERANGE);
        }
        
        let mut data = KVec::with_capacity(self.total_bytes - offset, GFP_KERNEL)?;
        
        // Handle first slice
        if offset < self.slices.0.len() {
            data.extend_from_slice(&self.slices.0[offset..], GFP_KERNEL)?;
            offset = 0;
        } else {
            offset -= self.slices.0.len();
        }

        // Handle second slice if exists
        if let Some(ref slice) = self.slices.1 {
            data.extend_from_slice(&slice[offset..], GFP_KERNEL)?;
        }

        Ok(data)
    }

    fn get_pos_from_offset(&self, mut offset: usize) -> Result<(usize, usize)> {
        if offset >= self.total_bytes {
            return Err(ERANGE);
        }

        // Check first slice
        if offset < self.slices.0.len() {
            return Ok((0, offset));
        }
        offset -= self.slices.0.len();

        // Check second slice if exists
        if let Some(ref slice) = self.slices.1 {
                if offset < slice.len() {
                return Ok((1, offset));
            }
        }

        Err(ERANGE)
    }

    // Get a raw pointer from the SBuffer to T. Will fail if the T is too large
    // to fit within one slice or the slice is too small to contain T.
    //
    // TODO(joelaf): I wonder if there is a better way to do this.
    // Can we use FromBytes to return the actual type, and prevent the
    // passage of raw pointers?
    //
    pub(crate) fn as_ptr<T>(&self, offset: usize) -> Result<*const T> {
        let (idx, slice_offset) = self.get_pos_from_offset(offset)?;
        
        let slice = self.idx_to_slice(idx).ok_or(ERANGE)?;

        // The pointer must be contained within a single contiguous slice of memory
        if size_of::<T>() > slice.len() - slice_offset {
            return Err(ERANGE);
        }

        Ok(slice[slice_offset..].as_ptr() as *const T)
    }

    pub(crate) fn iter_mut<'b>(&'b mut self) -> SBufferIteratorMut<'a, 'b> {
        SBufferIteratorMut { sbuf: self, pos: 0 }
    }

    // TODO(joelaf): Can the iterator not have its own shorter lifetime similar iter_mut?
    pub(crate) fn iter(&'a self) -> SBufferIterator<'a> {
        SBufferIterator { sbuf: self, pos: 0 }
    }
}

pub(crate) struct SBufferIteratorMut<'a, 'b> {
    sbuf: &'b mut SBuffer<'a>,
    pos: usize,
}

impl SBufferIteratorMut<'_, '_> {
    pub(crate) fn write_slice(&mut self, data: &[u8]) -> Result {
        self.sbuf.write(self.pos, data)?;
        self.pos += data.len();
        Ok(())
    }

    pub(crate) fn write_byte(&mut self, byte: u8) -> Result {
        self.sbuf.write(self.pos, &[byte])?;
        self.pos += size_of::<u8>();
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn write_word(&mut self, word: u16) -> Result {
        self.sbuf.write(self.pos, &word.to_le_bytes())?;
        self.pos += size_of::<u16>();
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn write_dword(&mut self, dword: u32) -> Result {
        self.sbuf.write(self.pos, &dword.to_le_bytes())?;
        self.pos += size_of::<u32>();
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn write_bytes(&mut self, data: &[u8]) -> Result {
        self.sbuf.write(self.pos, data)?;
        self.pos += data.len();
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn write_str(&mut self, s: &str) -> Result {
        self.sbuf.write(self.pos, s.as_bytes())?;
        self.pos += s.as_bytes().len();
        Ok(())
    }
}

pub(crate) struct SBufferIterator<'a> {
    sbuf: &'a SBuffer<'a>,
    pos: usize,
}

impl<'a> Iterator for SBufferIterator<'a> {
    type Item = u8;

    fn next(&mut self) -> Option<Self::Item> {
        let result = match self.sbuf.read_byte(self.pos) {
            Ok(byte) => Some(byte),
            Err(_) => None,
        };
        self.pos += 1;
        result
    }
}

impl<'a> DoubleEndedIterator for SBufferIterator<'a> {
    fn next_back(&mut self) -> Option<Self::Item> {
        if self.pos == self.sbuf.total_bytes {
            return None;
        }

        let result = match self.sbuf.read_byte(self.sbuf.total_bytes - self.pos - 1) {
            Ok(byte) => Some(byte),
            Err(_) => None,
        };
        self.pos += 1;
        result
    }
}
