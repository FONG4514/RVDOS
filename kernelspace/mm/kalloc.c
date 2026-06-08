/*
 * Derived from xv6-riscv (https://github.com/mit-pdos/xv6-riscv)
 * Copyright (c) 2006-2023 Frans Kaashoek, Robert Morris, Russ Cox, 
 *                         Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <kernel.h>

extern char end[]; // first address after kernel.

struct {
  spinlock_t lock;
  unsigned char *bitmap;
  uint64 start;
  int npage;
} kmem;

void kinit() {
  init_lock(&kmem.lock, "kmem");
  
  kmem.start = (uint64)PGROUNDUP((uint64)end);
  kmem.npage = (PHYSTOP - kmem.start) / PGSIZE;
  
  // Use the first page for the bitmap.
  // A 4KB bitmap can manage 32768 pages (128MB).
  kmem.bitmap = (unsigned char*)kmem.start;
  
  // Initially mark all pages as used (1).
  memset(kmem.bitmap, 0xff, PGSIZE);

  // Mark pages from bitmap's end to PHYSTOP as free.
  // We skip the first page because it holds the bitmap.
  for (uint64 p = kmem.start + PGSIZE; p + PGSIZE <= PHYSTOP; p += PGSIZE) {
    kfree((void*)p);
  }
}

void kfree(void *pa) {
  uint64 addr = (uint64)pa;

  if (addr % PGSIZE != 0 || addr < kmem.start || addr >= PHYSTOP) {
    return;
  }

  int pageno = (addr - kmem.start) / PGSIZE;
  int byte_idx = pageno / 8;
  int bit_idx = pageno % 8;

  accquire_lock(&kmem.lock);
  kmem.bitmap[byte_idx] &= ~(1 << bit_idx);
  release_lock(&kmem.lock);
}

void *kalloc() {
  accquire_lock(&kmem.lock);

  for (int i = 0; i < kmem.npage; i++) {
    int byte_idx = i / 8;
    int bit_idx = i % 8;

    if (!(kmem.bitmap[byte_idx] & (1 << bit_idx))) {
      kmem.bitmap[byte_idx] |= (1 << bit_idx);
      release_lock(&kmem.lock);

      void *pa = (void*)(kmem.start + (uint64)i * PGSIZE);
      memset(pa, 5, PGSIZE); // Fill with junk to catch dangling refs
      return pa;
    }
  }

  release_lock(&kmem.lock);
  return 0;
}
