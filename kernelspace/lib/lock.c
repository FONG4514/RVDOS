#include <kernel.h>

void init_lock(spinlock_t * lock , char * lock_name) {
    lock->lock_name = lock_name;
    lock->locked = 0;
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(spinlock_t *lock) {
  int r;
  r = (lock->locked && lock->lock_count == r_tp());
  return r;
}

void accquire_lock (spinlock_t * lock ) {

    push_off(lock);

    if(holding(lock)) {
      printf("Lock already held by CPU %d: %s\n", r_tp(), lock->lock_name);
      panic("acquire");
    }

    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        asm volatile("pause");
    }
    
    lock->lock_count = r_tp();
}

void release_lock (spinlock_t * lock) {
    if(!holding(lock))
      panic("release");

    lock->lock_count = -1;
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);

    pop_off(lock);
}