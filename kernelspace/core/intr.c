#include <kernel.h>

// push_off/pop_off are like intr_off/intr_on except they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, push_off, pop_off leaves them off.

void push_off(spinlock_t* lock) {
  int old = intr_get();

  intr_off();
  int id = r_tp();
  if(cpus[id].nlock == 0)
    cpus[id].intena = old;
  cpus[id].nlock += 1;
}

void pop_off(spinlock_t* lock) {
  int id = r_tp();
  if(intr_get())
    panic("pop_off - interruptible");
  if(cpus[id].nlock < 1)
    panic("pop_off");
  cpus[id].nlock -= 1;
  if(cpus[id].nlock == 0 && cpus[id].intena)
    intr_on();
}