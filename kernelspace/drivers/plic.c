#include<kernel.h>

// --- PLIC implementation ---
void plic_init() {
  // set UART's priority to 1.
  *(uint32*)(PLIC_PRIORITY + UART0_IRQ*4) = 1;
  // set VIRTIO's priority to 1.
  *(uint32*)(PLIC_PRIORITY + VIRTIO0_IRQ*4) = 1;
}

// 🌟 修改点：显式接收 hart id，不再内部盲目读取 r_tp()
void plic_inithart(int hart) {
  // set uart's enable bit for this hart's S-mode. 
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);
  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int plic_claim() {
  int hart = r_tp();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void plic_complete(int irq) {
  int hart = r_tp();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}