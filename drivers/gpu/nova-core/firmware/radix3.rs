// SPDX-License-Identifier: GPL-2.0

#![allow(dead_code, unused_variables)]

use core::alloc::Layout;

use kernel::bindings;
use kernel::device;
use kernel::error;
use kernel::page::PAGE_SIZE;
use kernel::prelude::*;
use kernel::types::ARef;

use crate::dma::DmaObject;
use crate::gsp::GSP_PAGE_SIZE;

pub(crate) struct Radix3 {
    lvl0: DmaObject,
    lvl1: DmaObject,
}

impl Radix3 {
    pub(crate) fn new(dev: &device::Device, size: usize) -> Result<Self> {
        Err(ENOTSUPP)
    }
}

pub(crate) struct RadixFirmware {
    // pub radix3: Radix3,
    dev: ARef<device::Device>,
    fw_sg_table: SgTable,
    lvl2_sg_table: SgTable,
    lvl1_sg_table: SgTable,
    lvl0: DmaObject,
    size: usize,
}

impl RadixFirmware {
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        name: &'static str,
        fw: &[u8],
    ) -> Result<Self> {
        pr_info!("GSP firmware has size {:#x}\n", fw.len());

        // Move the firmware into a vmalloc'd vector.
        let mut fw_vvec = VVec::with_capacity(fw.len(), GFP_KERNEL)?;
        fw_vvec.extend_from_slice(fw, GFP_KERNEL)?;

        let fw_sg_table = SgTable::new(dev, fw_vvec)?;
        pr_info!(
            "FW SG table has {} (from {}) entries\n",
            fw_sg_table.sg_table.nents,
            fw_sg_table.sg_table.orig_nents
        );

        let mut lvl2 = VVec::<u8>::with_capacity(
            fw_sg_table.num_pages * core::mem::size_of::<u64>(),
            GFP_KERNEL,
        )?;

        pr_info!(
            "lvl2 allocated with capacity {} size {}\n",
            lvl2.capacity(),
            lvl2.len()
        );

        map_into_lvl(&fw_sg_table, &mut lvl2)?;

        pr_info!(
            "lvl2 filled with capacity {} size {} or {} entries\n",
            lvl2.capacity(),
            lvl2.len(),
            lvl2.len() / core::mem::size_of::<u64>(),
        );

        let lvl2_sg_table = SgTable::new(dev, lvl2)?;
        pr_info!(
            "LVL2 SG table has {} (from {}) entries\n",
            lvl2_sg_table.sg_table.nents,
            lvl2_sg_table.sg_table.orig_nents
        );

        let mut lvl1 = VVec::<u8>::with_capacity(
            lvl2_sg_table.num_pages * core::mem::size_of::<u64>(),
            GFP_KERNEL,
        )?;

        pr_info!(
            "lvl1 allocated with capacity {} size {}\n",
            lvl1.capacity(),
            lvl1.len()
        );

        map_into_lvl(&lvl2_sg_table, &mut lvl1)?;

        pr_info!(
            "lvl1 filled with capacity {} size {} or {} entries\n",
            lvl1.capacity(),
            lvl1.len(),
            lvl1.len() / core::mem::size_of::<u64>(),
        );

        let lvl1_sg_table = SgTable::new(dev, lvl1)?;
        pr_info!(
            "LVL1 SG table has {} (from {}) entries\n",
            lvl1_sg_table.sg_table.nents,
            lvl1_sg_table.sg_table.orig_nents
        );

        let mut lvl0 = DmaObject::new(dev, GSP_PAGE_SIZE)?;
        let lvl0_slice =
            unsafe { core::slice::from_raw_parts_mut(lvl0.start_ptr_mut(), lvl0.size()) };
        lvl0_slice[0..core::mem::size_of::<u64>()].copy_from_slice(
            &(lvl1_sg_table.dma_iter().next().unwrap().dma_address as u64).to_le_bytes(),
        );

        pr_info!("LVL0 has DMA address {:#x}\n", lvl0.dma_handle());
        pr_info!(
            "First entry of LVL0: {:x}\n",
            u64::from_le_bytes(unsafe { *(lvl0.start_ptr().cast::<[u8; 8]>()) })
        );
        pr_info!(
            "LVL1 has DMA address {:#x}\n",
            lvl1_sg_table.dma_iter().next().unwrap().dma_address
        );
        pr_info!(
            "First entry of LVL1: {:x}\n",
            u64::from_le_bytes((&lvl1_sg_table.data[0..8]).try_into().unwrap())
        );
        pr_info!(
            "LVL2 has DMA address {:#x}\n",
            lvl2_sg_table.dma_iter().next().unwrap().dma_address
        );
        pr_info!(
            "First entry of LVL2: {:x}\n",
            u64::from_le_bytes((&lvl2_sg_table.data[0..8]).try_into().unwrap())
        );

        Ok(Self {
            dev: dev.into(),
            fw_sg_table,
            lvl2_sg_table,
            lvl1_sg_table,
            lvl0,
            size: fw.len(),
        })
    }

    pub(crate) fn lvl0_dma_handle(&self) -> bindings::dma_addr_t {
        self.lvl0.dma_handle()
    }

    pub(crate) fn size(&self) -> usize {
        self.size
    }
}

fn map_into_lvl(sg_table: &SgTable, dst: &mut VVec<u8>) -> Result {
    for sg_entry in sg_table.dma_iter() {
        pr_debug!(
            "sl: {:#x} {:#x}\n",
            sg_entry.dma_address,
            sg_entry.dma_length
        );
        // Round the size up to the next full page, if needed.
        let rounded_up_length = Layout::from_size_align(sg_entry.dma_length, GSP_PAGE_SIZE)
            .map_err(|_| EINVAL)?
            .pad_to_align()
            .size();
        for i in 0..(rounded_up_length / GSP_PAGE_SIZE) {
            let entry = sg_entry.dma_address + (GSP_PAGE_SIZE as u64 * i as u64);
            let entry_bytes = entry.to_le_bytes();
            dst.extend_from_slice(&entry_bytes, GFP_KERNEL)?;
        }
    }

    Ok(())
}

/// An owned [`VVec`] that is mapped into the address space of a given device.
struct SgTable {
    dev: ARef<device::Device>,
    sg_table: bindings::sg_table,
    data: VVec<u8>,
    num_pages: usize,
}

struct DmaSgEntry {
    dma_address: bindings::dma_addr_t,
    dma_length: usize,
}

struct DmaIterator<'a> {
    sg_table: &'a SgTable,
    curr_entry: u32,
    total_length: usize,
    curr_length: usize,
}

impl<'a> Iterator for DmaIterator<'a> {
    type Item = DmaSgEntry;

    fn next(&mut self) -> Option<Self::Item> {
        if self.curr_entry >= self.sg_table.sg_table.nents || self.curr_length >= self.total_length
        {
            None
        } else {
            let scatterlist =
                unsafe { self.sg_table.sg_table.sgl.offset(self.curr_entry as isize) };
            let dma_length = unsafe { (*scatterlist).dma_length } as usize;
            let res = DmaSgEntry {
                dma_address: unsafe { (*scatterlist).dma_address },
                dma_length,
            };
            self.curr_entry += 1;
            self.curr_length += dma_length;
            Some(res)
        }
    }
}

unsafe impl Send for SgTable {}

impl SgTable {
    fn new(dev: &device::Device, mut data: VVec<u8>) -> Result<Self> {
        // Round the size of the data up to the next page.
        let rounded_capacity = Layout::from_size_align(data.len(), PAGE_SIZE)
            .map_err(|_| EINVAL)?
            .pad_to_align()
            .size();
        // Ensure that the vector's memory covers whole pages.
        data.reserve(rounded_capacity - data.len(), GFP_KERNEL)?;
        let num_pages = rounded_capacity / PAGE_SIZE;

        // List of all the memory pages of `data`.
        let mut page_vec: KVec<*mut bindings::page> = KVec::with_capacity(num_pages, GFP_KERNEL)?;
        for page in (0..num_pages)
            .into_iter()
            .map(|page_idx| unsafe { data.as_ptr().offset((PAGE_SIZE * page_idx) as isize) })
            .map(|ptr| ptr.cast::<core::ffi::c_void>())
            .map(|ptr| unsafe { bindings::vmalloc_to_page(ptr) })
        {
            page_vec.push(page, GFP_KERNEL)?;
        }

        // Create a SG table for `data` and map it for `dev`.
        let mut sg_table: bindings::sg_table = Default::default();

        let ret = error::to_result(unsafe {
            bindings::sg_alloc_table_from_pages_segment(
                &mut sg_table,
                page_vec.as_mut_ptr(),
                page_vec.len() as u32,
                0,
                data.len(),
                u32::MAX,
                bindings::GFP_KERNEL,
            )
        })?;

        let ret = error::to_result(unsafe {
            bindings::dma_map_sgtable(
                dev.as_raw(),
                &mut sg_table,
                bindings::dma_data_direction_DMA_TO_DEVICE,
                bindings::DMA_ATTR_NO_WARN as usize,
            )
        })
        .inspect_err(|_| unsafe { bindings::sg_free_table(&mut sg_table) })?;

        Ok(Self {
            dev: dev.into(),
            sg_table,
            data,
            num_pages,
        })
    }

    fn dma_iter(&self) -> DmaIterator<'_> {
        DmaIterator {
            sg_table: self,
            curr_entry: 0,
            curr_length: 0,
            total_length: self.data.len(),
        }
    }
}

impl Drop for SgTable {
    fn drop(&mut self) {
        unsafe {
            bindings::dma_unmap_sg_attrs(
                self.dev.as_raw(),
                self.sg_table.sgl,
                self.sg_table.orig_nents as i32,
                bindings::dma_data_direction_DMA_TO_DEVICE,
                0,
            )
        };
        unsafe { bindings::sg_free_table(&mut self.sg_table) };
    }
}
