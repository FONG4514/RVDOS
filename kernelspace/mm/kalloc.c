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
