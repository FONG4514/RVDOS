#include <kernel.h>

#define MIN_SIZE 8
#define MAX_BUCKET 2048
#define MAGIC 0xdeadbeef
#define ALIGN 16
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(ALIGN - 1))
#define FREED 0x0

// Internal bucket structure is now compatible with kmem_cache
static struct kmem_cache buckets[9];
static uint32 bucket_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};

void kmalloc_init() {
  for (int i = 0; i < 9; i++) {
    init_lock(&buckets[i].lock, "kmalloc_bucket");
    buckets[i].objsize = bucket_sizes[i];
    buckets[i].freelist = 0;
  }
}

// kmem_cache implementation
struct kmem_cache* kmem_cache_create(char *name, uint32 size) {
    // We use kalloc to allocate the cache descriptor itself
    struct kmem_cache *cache = (struct kmem_cache*)kalloc();
    if (!cache) return 0;
    
    init_lock(&cache->lock, name);
    cache->pages = 0;
    cache->objsize = size;
    cache->freelist = 0;
    return cache;
}

void* kmem_cache_alloc(struct kmem_cache *cache) {
    if (!cache) return 0;
    
    uint32 actual_size = ALIGN_UP(cache->objsize + sizeof(struct header));
    accquire_lock(&cache->lock);

    if (cache->freelist == 0) {
        // Cache is empty, allocate a new page
        char *page = kalloc();
        if (page == 0) {
            release_lock(&cache->lock);
            return 0;
        }
        memset(page,0,PGSIZE);

        struct slab_page *sp = (struct slab_page*)kalloc();
        if (!sp) {
          kfree(page);
          release_lock(&cache->lock);
          return 0;
        }
        sp->addr = page;
        sp->next = cache->pages;
        cache->pages = sp;
        
        // Split page into blocks
        for (int j = 0; j + actual_size <= PGSIZE; j += actual_size) {
            struct block *bl = (struct block *)(page + j);
            bl->next = cache->freelist;
            cache->freelist = bl;
        }
    }

    struct header *h = (struct header *)cache->freelist;
    cache->freelist = cache->freelist->next;
    release_lock(&cache->lock);

    h->size = cache->objsize;
    h->magic = MAGIC;

    return (void *)(h + 1);
}

void kmem_cache_free(struct kmem_cache *cache, void *addr) {
    if (!cache || !addr) return;

    struct header *h = (struct header *)addr - 1;

    accquire_lock(&cache->lock);

    if (h->magic != MAGIC) {
        release_lock(&cache->lock);
        panic("double free or invalid free");
    }

    h->magic = FREED;

    struct block *bl = (struct block *)h;
    bl->next = cache->freelist;
    cache->freelist = bl;

    release_lock(&cache->lock);
}

// Legacy kmalloc/kmfree redirected to buckets
static int get_bucket_idx(uint32 size) {
  for (int i = 0; i < 9; i++) {
    if (size <= bucket_sizes[i]) return i;
  }
  return -1;
}

void *kmalloc(uint32 size) {
  if (size == 0) return 0;

  if (size > MAX_BUCKET) {
    return kalloc();
  }

  int idx = get_bucket_idx(size);
  return kmem_cache_alloc(&buckets[idx]);
}

void kmfree(void *addr) {
  if (addr == 0) return;

  if (((uint64)addr % PGSIZE) == 0) {
    kfree(addr);
    return;
  }

  struct header *h = (struct header *)addr - 1;
  if (h->magic != MAGIC) return;

  int idx = get_bucket_idx(h->size);
  if (idx == -1) return; // Should not happen if size is valid

  kmem_cache_free(&buckets[idx], addr);
}

void kmem_cache_destroy(struct kmem_cache *cache) {
    if (!cache) return;

    accquire_lock(&cache->lock);

    struct slab_page *p = cache->pages;
    while (p) {
        struct slab_page *next = p->next;
        kfree(p->addr);   // 释放 slab page
        kfree(p);         // 释放记录结构
        p = next;
    }

    cache->freelist = 0;
    release_lock(&cache->lock);

    kfree(cache); // 最后释放 cache 本身
}