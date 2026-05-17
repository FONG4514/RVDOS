#include <kernel.h>

pagetable_t kernel_pagetable;

// Returns the address of the PTE in pagetable for virtual address va.
// If alloc!=0, it creates any required page-table pages.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va > MAXVA)
    return 0;

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
        return 0;
      // memset(pagetable, 0, PGSIZE); // Clear new page table page
      for (int i = 0; i < 512; i++) pagetable[i] = 0; // simple clear
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Map a virtual address range to a physical address range.
int mappages(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int perm) {
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V) {
      printf("remap: va %p, pa %p\n", a, pa);
      return -1;
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

//Initial identity mapping for kernel
void kvminit() {
  kernel_pagetable = (pagetable_t)kalloc();
  for (int i = 0; i < 512; i++) kernel_pagetable[i] = 0;

  extern char etext[], fs_start[], fs_end[], erodata[];

  // 1. 映射内核代码段 [.text_start, etext)
  // 范围：0x80000000 -> etext
  mappages(kernel_pagetable, 0x80000000, 0x80000000, (uint64)etext - 0x80000000, PTE_R | PTE_X);

  // 2. 映射 LibOS 段 [fs_start, fs_end)
  // 因为你在链接脚本里紧跟在 .text 后面
  mappages(kernel_pagetable, (uint64)fs_start, (uint64)fs_start, (uint64)fs_end - (uint64)fs_start, PTE_R | PTE_W | PTE_X);

  // 3. 映射中间段 (如果有的话)
  if ((uint64)erodata > (uint64)fs_end) {
      mappages(kernel_pagetable, (uint64)fs_end, (uint64)fs_end, (uint64)erodata - (uint64)fs_end, PTE_R);
  }

  // 4. 映射剩余的 RAM (Data + Bss + Free Memory)
  // 范围：erodata -> PHYSTOP
  // 这步会自动包含 .data 和 .bss，因为它们在 erodata 之后
  uint64 ram_sz = PHYSTOP - (uint64)erodata;
  mappages(kernel_pagetable, (uint64)erodata, (uint64)erodata, ram_sz, PTE_R | PTE_W);

  // 5. 映射外设 (UART, VirtIO, CLINT, PLIC, SYSCON)
  mappages(kernel_pagetable, 0x10000000, 0x10000000, PGSIZE, PTE_R | PTE_W);
  mappages(kernel_pagetable, 0x10001000, 0x10001000, PGSIZE, PTE_R | PTE_W);
  mappages(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  mappages(kernel_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  mappages(kernel_pagetable, SYSCON, SYSCON, PGSIZE, PTE_R | PTE_W);
}

// Map kernel regions into a user page table.
// Does NOT set PTE_U, so user mode cannot access them,
// but S-mode can access them without switching satp.
void uvmmap_kernel(pagetable_t upgtbl) {
  // Map kernel code and data, including the whole RAM up to PHYSTOP
  // We map from 0x80000000 up to PHYSTOP.
  mappages(upgtbl, 0x80000000, 0x80000000, PHYSTOP - 0x80000000, PTE_R | PTE_W | PTE_X);

  // 2. Map MMIO regions
  mappages(upgtbl, 0x10000000, 0x10000000, PGSIZE, PTE_R | PTE_W); // UART
  mappages(upgtbl, 0x10001000, 0x10001000, PGSIZE, PTE_R | PTE_W); // VirtIO
  mappages(upgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);          // CLINT
  mappages(upgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);          // PLIC
  mappages(upgtbl, SYSCON, SYSCON, PGSIZE, PTE_R | PTE_W);        // SYSCON
}

// Function to create a user page table and map trapframe
pagetable_t uvmcreate(user_context_t *context) {
  pagetable_t pt = (pagetable_t)kalloc();
  if (pt == 0) return 0;
  for (int i = 0; i < 512; i++) pt[i] = 0;

  // Map kernel parts for fast system calls (no PTE_U)
  uvmmap_kernel(pt);

  // Map trapframe (user context) - S-mode uses this to save/restore registers.
  // We don't give it PTE_U so S-mode can access it without setting sstatus.SUM.
  mappages(pt, TRAPFRAME, (uint64)context, PGSIZE, PTE_R | PTE_W);

  return pt;
}

// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      continue; // Skip areas where the page table structure itself doesn't exist
    if((*pte & PTE_V) == 0)
      continue; // Skip pages that are not mapped
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}


void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  // 1. 释放用户程序占用的物理内存 (从 USERBASE 开始)
  if(sz > USERBASE)
    uvmunmap(pagetable, USERBASE, (PGROUNDUP(sz) - USERBASE)/PGSIZE, 1);

  // 2. 释放用户栈 (在 USTACK_TOP 之下的一个页面)
  uvmunmap(pagetable, USTACK_TOP - PGSIZE, 1, 1);

  // 3. 解除内核镜像和全内存映射 (do_free = 0)
  uvmunmap(pagetable, 0x80000000, (PHYSTOP - 0x80000000)/PGSIZE, 0);

  // 4. 解除 MMIO 映射 (do_free = 0)
  uvmunmap(pagetable, 0x10000000, 1, 0); // UART
  uvmunmap(pagetable, 0x10001000, 1, 0); // VirtIO
  uvmunmap(pagetable, CLINT, 0x10000/PGSIZE, 0);
  uvmunmap(pagetable, PLIC, 0x400000/PGSIZE, 0);
  uvmunmap(pagetable, SYSCON, 1, 0);

  // 5. 解除高地址映射 (do_free = 0)
  uvmunmap(pagetable, TRAPFRAME, 1, 0);

  // 6. 现在可以安全拆除页表结构了
  freewalk(pagetable);
}


// Enable paging
void kvminithart() {
  sfence_vma();
  uint64 x = r_satp();
  x &= ~((1L << 60) - 1); // clear PPN
  x |= (8L << 60);         // set mode SV39
  x |= ((uint64)kernel_pagetable >> 12);
  w_satp(x);
  sfence_vma();
  }