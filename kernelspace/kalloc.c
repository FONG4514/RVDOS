#include "defs.h"
#include "riscv.h"

extern char end[]; // first address after kernel.

struct run {
  struct run *next;
};

struct {
  struct run *freelist;
  spinlock_t mem_lock;
} kmem;

void kinit() {
  
  init_lock(&kmem.mem_lock,"memlock");

  kmem.freelist = 0;
  // free memory from end of kernel to PHYSTOP
  for (char *p = (char*)PGROUNDUP((uint64)end); p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE) {
    kfree(p);
  }
}

void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) {
    return;
  }

  r = (struct run*)pa;

  accquire_lock(&kmem.mem_lock);

  r->next = kmem.freelist;
  kmem.freelist = r;

  release_lock(&kmem.mem_lock);
}

void *kalloc() {
  struct run *r;

  accquire_lock(&kmem.mem_lock);

  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
  }

  release_lock(&kmem.mem_lock);

  return (void*)r;
  
}
