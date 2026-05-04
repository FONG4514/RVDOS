#include <kernel.h>

#define MIN_SIZE 8
#define MAX_BUCKET 2048

struct header {
  uint32 size;
  uint32 magic;
};

#define MAGIC 0xdeadbeef

struct block {
  struct block *next;
};

struct bucket {
  spinlock_t lock;
  struct block *freelist;
  uint32 size;
};

// Buckets for sizes: 8, 16, 32, 64, 128, 256, 512, 1024, 2048
static struct bucket buckets[9];
static uint32 bucket_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

void kmalloc_init() {
  for (int i = 0; i < 9; i++) {
    init_lock(&buckets[i].lock, "kmalloc_bucket");
    buckets[i].size = bucket_sizes[i];
    buckets[i].freelist = 0;
  }
}

static int get_bucket_idx(uint32 size) {
  for (int i = 0; i < 9; i++) {
    if (size <= bucket_sizes[i]) return i;
  }
  return -1;
}

void *kmalloc(uint32 size) {
  if (size == 0) return 0;

  // For sizes > 2048, use kalloc directly
  if (size > MAX_BUCKET) {
    return kalloc();
  }

  int idx = get_bucket_idx(size);
  struct bucket *b = &buckets[idx];
  uint32 actual_size = b->size + sizeof(struct header);

  accquire_lock(&b->lock);

  if (b->freelist == 0) {
    // Bucket is empty, allocate a new page
    char *page = kalloc();
    if (page == 0) {
      release_lock(&b->lock);
      return 0;
    }
    
    // Split page into blocks
    for (int j = 0; j + actual_size <= PGSIZE; j += actual_size) {
      struct block *bl = (struct block *)(page + j);
      bl->next = b->freelist;
      b->freelist = bl;
    }
  }

  struct header *h = (struct header *)b->freelist;
  b->freelist = b->freelist->next;

  release_lock(&b->lock);

  h->size = b->size;
  h->magic = MAGIC;

  return (void *)(h + 1);
}

void kmfree(void *addr) {
  if (addr == 0) return;

  // Check if it's a page-aligned address (likely from kalloc directly)
  if (((uint64)addr % PGSIZE) == 0) {
    kfree(addr);
    return;
  }

  struct header *h = (struct header *)addr - 1;
  if (h->magic != MAGIC) {
    // If not magic, it might be a page allocation that wasn't aligned? 
    // Or just a kfree call. For safety, if it's not our heap block, we can't do much.
    // In a real OS, we'd check page metadata.
    return;
  }

  int idx = get_bucket_idx(h->size);
  struct bucket *b = &buckets[idx];

  accquire_lock(&b->lock);
  struct block *bl = (struct block *)h;
  bl->next = b->freelist;
  b->freelist = bl;
  release_lock(&b->lock);
}
