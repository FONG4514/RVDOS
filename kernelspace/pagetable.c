#include "riscv.h"
#include "defs.h"

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

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can be used to check if a user address is valid.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  // 必须加上虚拟地址在页面内的偏移量
  return pa | (va & (PGSIZE - 1));
}

//Initial identity mapping for kernel
void kvminit() {
  kernel_pagetable = (pagetable_t)kalloc();
  for (int i = 0; i < 512; i++) kernel_pagetable[i] = 0;

  extern char etext[], fs_start[], fs_end[], erodata[], trampoline_start[];

  // 1. 映射内核代码段 [.text_start, etext)
  // 范围：0x80000000 -> etext
  mappages(kernel_pagetable, 0x80000000, 0x80000000, (uint64)etext - 0x80000000, PTE_R | PTE_X);

  // 2. 映射 LibOS 段 [fs_start, fs_end)
  // 因为你在链接脚本里紧跟在 .text 后面
  mappages(kernel_pagetable, (uint64)fs_start, (uint64)fs_start, (uint64)fs_end - (uint64)fs_start, PTE_R | PTE_W | PTE_X);

  // 3. 映射跳板页相关的中间段 (如果有的话)
  // 注意：Trampoline 通常在最顶端映射，但物理上它在镜像里也有占位
  // 如果 fs_end 到 erodata 之间还有东西（比如 .trampoline 在物理内存的占位），也要映射
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

  // 6. 映射虚拟地址顶端的 TRAMPOLINE
  // 这是一个高地址映射，物理地址指向代码镜像里的位置
  mappages(kernel_pagetable, TRAMPOLINE, (uint64)trampoline_start, PGSIZE, PTE_R | PTE_X);
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

// Function to create a user page table and map trampoline/trapframe
pagetable_t uvmcreate(user_context_t *context) {
  pagetable_t pt = (pagetable_t)kalloc();
  if (pt == 0) return 0;
  for (int i = 0; i < 512; i++) pt[i] = 0;

  extern char trampoline_start[];
  
  // Map trampoline (with PTE_R | PTE_X)
  // This is required for entering/exiting kernel.
  if(mappages(pt, TRAMPOLINE, (uint64)trampoline_start, PGSIZE, PTE_R | PTE_X) < 0){
    // TODO: free pt
    return 0;
  }
  
  // Map trapframe (user context) - S-mode uses this to save/restore registers.
  // We map it in user page table so user mode code can't see it (no PTE_U),
  // but it's at a fixed address for user_vector to find.
  if(mappages(pt, TRAPFRAME, (uint64)context, PGSIZE, PTE_R | PTE_W) < 0){
    // TODO: free pt
    return 0;
  }

  return pt;
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