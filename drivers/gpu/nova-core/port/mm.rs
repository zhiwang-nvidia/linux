#![allow(dead_code)]

use kernel::new_mutex;
use kernel::prelude::*;
use kernel::sync::{Arc, Mutex};
use core::cell::UnsafeCell;
use kernel::list::{
    List, ListArc, ListLinks, AtomicTracker, TryNewListArc,
};
use kernel::sync::UniqueArc;
use crate::{roundup, rounddown};

const NVKM_MM_TYPE_NONE: u8 = 0x00;
const NVKM_MM_TYPE_HOLE: u8 = 0xff;

#[repr(C)]
#[pin_data]
pub(crate) struct MemRangeNode {
    #[pin]
    nodes: ListLinks<0>,
    #[pin]
    free: ListLinks<1>,
    heap: u8,
    mm_type: UnsafeCell<u8>,
    offset: UnsafeCell<usize>,
    length: UnsafeCell<usize>,

    #[pin]
    free_tracker: AtomicTracker<1>,
}

kernel::list::impl_has_list_links! {
    impl HasListLinks<0> for MemRangeNode { self.nodes }
}
kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for MemRangeNode { untracked; }
}
kernel::list::impl_list_item! {
    impl ListItem<0> for MemRangeNode {
        using ListLinks;
    }
}

kernel::list::impl_has_list_links! {
    impl HasListLinks<1> for MemRangeNode { self.free }
}
kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<1> for MemRangeNode {
        tracked_by free_tracker: AtomicTracker<1>;
    }
}

kernel::list::impl_list_item! {
    impl ListItem<1> for MemRangeNode {
        using ListLinks;
    }
}

#[repr(C)]
#[pin_data]
pub(crate) struct MemRangeInner {
    #[pin]
    nodes: List<MemRangeNode, 0>,
    #[pin]
    free: List<MemRangeNode, 1>,
    block_size: usize,
    heap_nodes: usize,
}

pub(crate) struct MemRange {
    inner: Arc<Mutex<MemRangeInner>>,
}

impl MemRangeNode {

    pub(crate) fn wrap(offset: usize, length: usize) -> Result<Arc<MemRangeNode>> {
        let unique = UniqueArc::try_pin_init(
            try_pin_init!(MemRangeNode {
                nodes <- ListLinks::new(),
                free <- ListLinks::new(),
                heap: 0,
                mm_type: 0.into(),
                offset: offset.into(),
                length: length.into(),
                free_tracker <- AtomicTracker::new(),
            }), GFP_KERNEL)?;
        Ok(unique.into())
    }

    pub(crate) fn new(heap: u8, mm_type: u8, offset: usize, length: usize) -> Result<(ListArc<Self, 0>, ListArc<Self, 1>)> {
        let unique = UniqueArc::try_pin_init(
            try_pin_init!(MemRangeNode {
                nodes <- ListLinks::new(),
                free <- ListLinks::new(),
                heap,
                mm_type: mm_type.into(),
                offset: offset.into(),
                length: length.into(),
                free_tracker <- AtomicTracker::new(),
            }), GFP_KERNEL)?;
        let (arc1, arc2): (ListArc<Self, 0>, ListArc<Self, 1>) = ListArc::<Self, 0>::pair_from_pin_unique::<1>(unique);
        Ok((arc1, arc2))
    }

    pub(crate) fn heap(&self) -> u8 {
        self.heap
    }

    pub(crate) fn mm_type(&self) -> u8 {
        unsafe { *self.mm_type.get() }
    }

    pub(crate) fn size(&self) -> usize {
        unsafe { *self.length.get() }
    }

    pub(crate) fn addr(&self) -> usize {
        unsafe { *self.offset.get() }
    }
}

#[allow(dead_code)]
impl MemRangeInner {


    pub(crate) fn node_prev(list: &mut List<MemRangeNode, 0>, node: Arc<MemRangeNode>) -> Option<Arc<MemRangeNode>> {
        let cursor = list.cursor_front();

        let mut cursor = match cursor {
            None => { return None; }
            Some(x) => { x }
        };

        loop {
            if cursor.eq(node.as_ref()) {
                break;
            }

            cursor = match cursor.next() {
                None => { return None; }
                Some(x) => { x}
            };

        }

        match cursor.prev() {
            None => None,
            Some(x) => Some(x.current().into())
        }
    }

    pub(crate) fn node_next(list: &mut List<MemRangeNode, 0>, node: Arc<MemRangeNode>) -> Option<Arc<MemRangeNode>> {
        let cursor = list.cursor_front();

        let mut cursor = match cursor {
            None => { return None; }
            Some(x) => { x }
        };

        loop {
            if cursor.eq(node.as_ref()) {
                break;
            }

            cursor = match cursor.next() {
                None => { return None; }
                Some(x) => { x}
            };

        }

        match cursor.next() {
            None => None,
            Some(x) => Some(x.current().into())
        }
    }

    pub(crate) fn new(block: usize) -> Result<Arc<Mutex<MemRangeInner>>> {

        let range = Arc::pin_init(new_mutex!(Self {
            nodes: List::new(),
            free: List::new(),
            block_size: block,
            heap_nodes: 0
        }), GFP_KERNEL)?;

        Ok(range)
    }

    pub(crate) fn dump(&self) {
        for node in &self.nodes {
            pr_info!("node: {:#x} {:#x} {}\n", node.addr(), node.size(), node.mm_type());
        }
        for node in &self.free {
            pr_info!("free: {:#x} {:#x} {}\n", node.addr(), node.size(), node.mm_type());
        }
    }


    pub(crate) fn size(&self, heap: u8) -> Result<usize> {
        let mut size: usize = 0;
        for node in &self.nodes {
            if node.heap() == heap {
                size += node.size();
            }
        }
        Ok(size)
    }

    pub(crate) fn init(&mut self, heap: u8, offset: usize, length: usize) -> Result<()> {

        let mut node_offset = offset;
        let mut node_length = length;

        pr_info!("node {} {}\n", node_offset, node_length);
        if self.heap_nodes > 0 {
            let prev_cursor = self.nodes.cursor_front().unwrap().end().unwrap();
            let prev = prev_cursor.current();

            let next = prev.addr() + prev.size();

            if next != offset {
                let (node1, _) = MemRangeNode::new(heap, NVKM_MM_TYPE_HOLE, next, offset - next)?;
                self.nodes.push_back(node1);
            }
        }
        if length != 0 {
            node_offset = roundup(node_offset, self.block_size);
            node_length = rounddown(offset + length, self.block_size);

            pr_info!("node {} {}\n", node_offset, node_length);
            node_length -= node_offset;
        }

        let (node1, node2) = MemRangeNode::new(heap, 0, node_offset, node_length)?;

        self.heap_nodes += 1;
        self.nodes.push_back(node1);
        self.free.push_back(node2);
        Ok(())
    }

    fn region_head(&mut self, a: Arc<MemRangeNode>, mm_type: u8, splitsize: usize, size: usize) -> Result<Arc<MemRangeNode>> {

        if splitsize != 0 && a.size() != splitsize {
            let (node1, node2) = MemRangeNode::new(a.heap, a.mm_type(), a.addr(), splitsize)?;
            let _node1_ref = node1.clone_arc();
            unsafe {
                *a.offset.get() += splitsize;
                *a.length.get() -= splitsize;
            }
            self.nodes.push_before(&a.nodes, node1);
            if a.mm_type() == NVKM_MM_TYPE_NONE {
                self.free.push_before(&a.free, node2);
            }
        }
        if a.size() == size {
            let _ = unsafe { self.free.remove(&a) };
            return Ok(Arc::<MemRangeNode>::from(a));
        }

        let (node1, _node2) = MemRangeNode::new(a.heap, mm_type, a.addr(), size)?;
        let node1_ref = node1.clone_arc();
        unsafe {
            *a.offset.get() += size;
            *a.length.get() -= size;
        }
        self.nodes.push_before(&a.nodes, node1);
        Ok(node1_ref)
    }

    pub(crate) fn head(&mut self, heap: u8, mm_type: u8, size_max: usize, size_min: usize, align: usize) -> Result<Arc<MemRangeNode>> {
        let mask: usize = align - 1;

        let this: Arc<MemRangeNode>;
        let splitoff;
        let newsize;

        {
            let mut next_cursor = self.free.cursor_front().unwrap();

            loop {
                if next_cursor.current().heap != heap {
                    next_cursor = match next_cursor.next() {
                        None => { return Err(ENOMEM); }
                        Some(cur) => cur
                    };
                    continue;
                }

                let mut e : usize = next_cursor.current().addr() + next_cursor.current().size();
                let mut s : usize = next_cursor.current().addr();

                s = (s + mask) & !mask;
                e &= !mask;
                if s > e || e - s < size_min {
                    next_cursor = match next_cursor.next() {
                        None => { return Err(ENOMEM); }
                        Some(cur) => cur
                    };
                    continue;
                }

                let curr = next_cursor.current();
                this = Arc::<MemRangeNode>::from(curr);
                splitoff = s - this.addr();
                newsize = core::cmp::min(size_max, e - s);
                break;
            }
        }
        let new = self.region_head(this, mm_type, splitoff, newsize)?;
        return Ok(new);
    }

    fn region_tail(&mut self, a: Arc<MemRangeNode>, mm_type: u8, csize: usize, size: usize) -> Result<Arc<MemRangeNode>> {
        if csize != 0 && a.size() != csize {
            unsafe { *a.length.get() -= csize };

            let (node1, node2) = MemRangeNode::new(a.heap, a.mm_type(), a.addr() + a.size(), csize)?;

            let _node1_ref = node1.clone_arc();

            self.nodes.push_after(&a.nodes, node1);
            if a.mm_type() == NVKM_MM_TYPE_NONE {
                self.free.push_after(&a.free, node2);
            }
        }

        if a.size() == size {
            unsafe { *a.mm_type.get() = mm_type };
            let _ = unsafe { self.free.remove(&a) };
            return Ok(a.clone());
        }

        unsafe { *a.length.get() -= size };

        let (node1, _node2) = MemRangeNode::new(a.heap, mm_type, a.addr() + a.size(), size)?;

        let node1_ref = node1.clone_arc();

        self.nodes.push_after(&a.nodes, node1);

        Ok(node1_ref)
    }

    pub(crate) fn tail(&mut self, heap: u8, mm_type: u8, size_max: usize, size_min: usize, align: usize) -> Result<Arc<MemRangeNode>> {
        let mask: usize = align - 1;

        let this;
        let csize;
        let asize;

        {
            let mut prev_cursor = self.free.cursor_front().unwrap().end().unwrap();

            loop {
                let mut e : usize = prev_cursor.current().addr() + prev_cursor.current().size();
                let mut s : usize = prev_cursor.current().addr();
                let mut c : usize = 0;
                let mut a;
                if prev_cursor.current().heap != heap {
                    prev_cursor = match prev_cursor.prev() {
                        None => { return Err(ENOMEM); }
                        Some(cur) => cur
                    };
                    continue;
                }

                let nthis = Arc::<MemRangeNode>::from(prev_cursor.current());
                let prev = Self::node_prev(&mut self.nodes, nthis.clone());
                match prev {
                    None => {},
                    Some(p) => {
                        if p.mm_type() != mm_type {
                            s = roundup(s, self.block_size);
                        }
                    }
                }

                let next = Self::node_next(&mut self.nodes, nthis.clone());
                match next {
                    None => {},
                    Some(n) => {
                        if n.mm_type() != mm_type {
                            e = rounddown(e, self.block_size);
                            c = n.addr() - e;
                        }
                    }
                }

                s = (s + mask) & !mask;
                a = e.wrapping_sub(s);
                if s > e || a < size_min {
                    prev_cursor = match prev_cursor.prev() {
                        None => { return Err(ENOMEM); }
                        Some(cur) => cur
                    };
                    continue;
                }

                a = core::cmp::min::<usize>(a, size_max);
                s = (e - a) & !mask;
                c += (e - s) - a;

                pr_info!("tail a:{:#x} s:{:#x} e:{:#x} c:{:#x}\n", a, s, e, c);

                this = Arc::<MemRangeNode>::from(prev_cursor.current());
                csize = c;
                asize = a;
                break;
            }
        }
        let new = self.region_tail(this, mm_type, csize, asize)?;
        return Ok(new);
    }

    fn free(&mut self, node: Arc<MemRangeNode>) {
        let prev = Self::node_prev(&mut self.nodes, node.clone());
        let next = Self::node_next(&mut self.nodes, node.clone());
        let mut this = Some(node);

        match prev {
            None => {}
            Some(p) => {
                if p.mm_type() == NVKM_MM_TYPE_NONE {
                    unsafe {
                        *p.length.get() += *this.as_ref().unwrap().length.get();
                    }
                    let _ = unsafe { self.nodes.remove(&this.unwrap()) };
                    this = Some(p);
                }
            }
        }

        match next {
            None => {}
            Some(n) => {
                if n.mm_type() == NVKM_MM_TYPE_NONE {
                    unsafe {
                        *n.offset.get() = *this.as_ref().unwrap().offset.get();
                        *n.length.get() += *this.as_ref().unwrap().length.get();
                    }
                    if this.as_ref().unwrap().mm_type() == NVKM_MM_TYPE_NONE {
                        let _ = unsafe { self.free.remove(this.as_ref().unwrap()) };
                    }
                    let _ = unsafe { self.nodes.remove(this.as_ref().unwrap()) };
                    this = None;
                }
            }
        }

        match this {
            None => {}
            Some(t) => {
                if t.mm_type() != NVKM_MM_TYPE_NONE {

                    let mut ptr: Arc<MemRangeNode>;
                    {
                        let mut cursor = self.free.cursor_front().unwrap();

                        loop {
                            ptr = Arc::<MemRangeNode>::from(cursor.current());
                            if t.addr() < cursor.current().addr() {
                                break;
                            }
                            cursor = match cursor.next() {
                                None => { break; }
                                Some(c) => c
                            };
                        }
                    }

                    unsafe { *t.mm_type.get() = NVKM_MM_TYPE_NONE };

                    match ListArc::try_from_arc(t) {
                        Err(_) => {},
                        Ok(listarc) => {
                            self.free.push_before(&ptr.free, listarc);
                        }
                    }
                }
            }
        }
    }
}

impl MemRange {

    pub(crate) fn new(block: usize) -> Result<MemRange> {
        Ok(Self {
            inner: MemRangeInner::new(block)?,
        })
    }

    pub(crate) fn init(&mut self, heap: u8, offset: usize, length: usize) -> Result<()> {
        let mut inner = self.inner.lock();
        inner.init(heap, offset, length)
    }

    pub(crate) fn size(&self, heap: u8) -> Result<usize> {
        let inner = self.inner.lock();
        inner.size(heap)
    }

    pub(crate) fn head(&self, heap: u8, mm_type: u8, size_max: usize, size_min: usize, align: usize) -> Result<Arc<MemRangeNode>> {
        let mut inner = self.inner.lock();
        inner.head(heap, mm_type, size_max, size_min, align)
    }

    pub(crate) fn tail(&self, heap: u8, mm_type: u8, size_max: usize, size_min: usize, align: usize) -> Result<Arc<MemRangeNode>> {
        let mut inner = self.inner.lock();
        inner.tail(heap, mm_type, size_max, size_min, align)
    }

    pub(crate) fn dump(&self) {
        let inner = self.inner.lock();
        inner.dump();
    }

    pub(crate) fn free(&self, node: Arc<MemRangeNode>) {
        let mut inner = self.inner.lock();
        inner.free(node);
    }
}
