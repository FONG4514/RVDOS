#include "defs.h"
#include "riscv.h"

#define FS_CODE __attribute__((section(".fs_code")))
#define FS_DATA __attribute__((section(".fs_data")))

// --- VirtIO Blk Definitions ---
#define VIRTIO0 0x10001000L
#define REG_V(off) ((volatile uint32 *)(VIRTIO0 + (off)))

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028  // Guest 页面大小 (仅用于 Legacy)
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c  // 队列对齐字节数 (仅用于 Legacy)
#define VIRTIO_MMIO_STATUS 0x070

#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1

struct virtq_desc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
};

struct virtq_avail {
  uint16 flags;
  uint16 idx;
  uint16 ring[16];
};

struct virtq_used_elem {
  uint32 id;
  uint32 len;
};

struct virtq_used {
  uint16 flags;
  uint16 idx;
  struct virtq_used_elem ring[16];
};

struct virtio_blk_req {
  uint32 type;
  uint32 reserved;
  uint64 sector;
};

static struct {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  uint8 *status;
  struct virtio_blk_req *ops;
} disk FS_DATA;


int FS_CODE virtio_disk_init() {
  uint32 magic = *REG_V(VIRTIO_MMIO_MAGIC_VALUE);
  uint32 ver = *REG_V(VIRTIO_MMIO_VERSION);
  uint32 dev = *REG_V(VIRTIO_MMIO_DEVICE_ID);

  // 只支持 Version 1 (Legacy)
  if(magic != 0x74726976 || ver != 1 || dev != 2){
    printf("VirtIO Legacy Check Failed: Magic=0x%x, Ver=%d, Dev=%d\n", magic, ver, dev);
    panic("virtio_disk_init: non-legacy device or no disk found\n");
  }

  uint32 status = 0;
  status |= VIRTIO_STATUS_ACKNOWLEDGE;
  *REG_V(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_STATUS_DRIVER;
  *REG_V(VIRTIO_MMIO_STATUS) = status;

  // Legacy 模式特性协商：通常简单地写 0
  *REG_V(VIRTIO_MMIO_DRIVER_FEATURES) = 0; 

  // 选择队列并确认大小
  *REG_V(VIRTIO_MMIO_QUEUE_SEL) = 0;
  uint32 max = *REG_V(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max < 16) panic("virtio_disk_init: queue too small");
  *REG_V(VIRTIO_MMIO_QUEUE_NUM) = 16;

  // 分配并清零内存 (恒等映射)
  void *p = kalloc(); 
  if(p == 0) return -1;
  uint64 *ptr = (uint64*)p;
  for(int i = 0; i < PGSIZE / 8; i++) ptr[i] = 0;

  // --- Legacy 内存布局控制 ---
  disk.desc = (struct virtq_desc*)p;
  disk.avail = (struct virtq_avail*)(p + 16 * sizeof(struct virtq_desc));
  
  // 关键：Legacy 硬件通过 QUEUE_ALIGN 计算 Used Ring 的位置
  // 我们手动对齐到页面的一半 (2048 字节)
  disk.used = (struct virtq_used*)(p + 2048);
  
  // 其他结构体放在页面末尾
  disk.status = (uint8*)(p + PGSIZE - 16);
  disk.ops = (struct virtio_blk_req*)(p + PGSIZE - 128);

  // --- 告知硬件 (Legacy 专用寄存器) ---
  *REG_V(VIRTIO_MMIO_GUEST_PAGE_SIZE) = 4096;
  *REG_V(VIRTIO_MMIO_QUEUE_ALIGN) = 2048; // 必须匹配 disk.used 的偏移
  *REG_V(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)p) >> 12; 

  status |= VIRTIO_STATUS_DRIVER_OK;
  *REG_V(VIRTIO_MMIO_STATUS) = status;
  
  printf("virtio_disk_init: Legacy Mode");
  return 0;
}

int FS_CODE disk_read(uint32 sector, uint8 *buf, uint32 count) {
  // 1. 清除上一次的状态并初始化数据
  disk.status[0] = 0xff; 
  disk.ops[0].type = VIRTIO_BLK_T_IN;
  disk.ops[0].reserved = 0;
  disk.ops[0].sector = sector;

  // 2. 填充描述符 (恒等映射直接用地址)
  // Header: 告诉硬件我们要读哪个扇区
  disk.desc[0].addr = (uint64)&disk.ops[0];
  disk.desc[0].len = sizeof(struct virtio_blk_req);
  disk.desc[0].flags = VIRTQ_DESC_F_NEXT;
  disk.desc[0].next = 1;

  // Buffer: 硬件把数据写到这里
  disk.desc[1].addr = (uint64)buf;
  disk.desc[1].len = 512 * count;
  disk.desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
  disk.desc[1].next = 2;

  // Status: 硬件最后写入结果 (0 为成功)
  disk.desc[2].addr = (uint64)&disk.status[0];
  disk.desc[2].len = 1;
  disk.desc[2].flags = VIRTQ_DESC_F_WRITE;
  disk.desc[2].next = 0;

  // 3. 提交到 Avail Ring
  // 记录当前的 idx，方便观察硬件是否处理
  uint16 old_idx = disk.avail->idx;
  disk.avail->ring[old_idx % 16] = 0; // 从 desc[0] 开始
  
  __sync_synchronize();
  disk.avail->idx += 1;
  __sync_synchronize();

  // 4. 通知硬件
  *REG_V(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; 

  // 5. 轮询等待，增加超时保护
  uint64 timeout = 1000000; // 根据你的时钟频率调整
  while(disk.status[0] == 0xff && timeout > 0) {
      timeout--;
      // 如果你的内核有简单的 delay 可以加在这里
  }

  // 6. 结果校验
  if(disk.status[0] == 0xff) {
      printf("[VirtIO Error] Timeout! Hardware did not respond.\n");
      printf("Check: QueuePFN=0x%x, StatusReg=0x%x\n", 
              *REG_V(VIRTIO_MMIO_QUEUE_PFN), *REG_V(VIRTIO_MMIO_STATUS));
      return -1;
  }

  if(disk.status[0] != 0) {
      printf("[VirtIO Error] Disk returned error status: %d\n", disk.status[0]);
      return -1;
  }

  // 7. 成功后打印 FAT32 关键特征进行验证
  if(sector == 0) {
      printf("Sector 0 Read. Magic: 0x%x%x (Expected 0x55aa) ", 
              buf[510], buf[511]);
      printf("OEM ID: %.8s\n", &buf[3]);
  }

  return 0;
}
// --- FAT32 Implementation ---
#define SECTOR_SIZE 512
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LONG_NAME 0x0F

typedef struct {
    uint8  jmp[3];
    uint8  oem_name[8];
    uint16 bytes_per_sector;
    uint8  sectors_per_cluster;
    uint16 reserved_sectors;
    uint8  fat_count;
    uint16 root_entry_count;
    uint16 total_sectors_16;
    uint8  media_type;
    uint16 fat_size_16;
    uint16 sectors_per_track;
    uint16 head_count;
    uint32 hidden_sectors;
    uint32 total_sectors_32;
    uint32 fat_size_32;
    uint16 ext_flags;
    uint16 fs_version;
    uint32 root_cluster;
    uint16 fs_info_sector;
    uint16 backup_boot_sector;
    uint8  reserved[12];
    uint8  drive_number;
    uint8  reserved1;
    uint8  boot_signature;
    uint32 volume_id;
    uint8  volume_label[11];
    uint8  file_system_type[8];
} __attribute__((packed)) FAT32_BPB;

typedef struct {
    uint8  name[8];
    uint8  ext[3];
    uint8  attr;
    uint8  reserved;
    uint8  create_time_tenth;
    uint16 create_time;
    uint16 create_date;
    uint16 last_access_date;
    uint16 first_cluster_hi;
    uint16 write_time;
    uint16 write_date;
    uint16 first_cluster_lo;
    uint32 file_size;
} __attribute__((packed)) FAT32_DirEntry;

struct fat32_fs {
    uint32 first_data_sector;
    uint32 fat_sector;
    uint32 sectors_per_cluster;
    uint32 root_cluster;
    uint32 fat_size;
    uint8 *cache_page;
};

static struct fat32_fs fs FS_DATA;
static file_t file_pool[64] FS_DATA;
static spinlock_t file_pool_lock;
static spinlock_t fs_lock; // Global lock for hardware and cache access

int disk_write(uint32 sector, uint8 *buf, uint32 count);
static uint32 alloc_cluster();
static int set_next_cluster(uint32 cluster, uint32 next);
static uint32 get_next_cluster(uint32 cluster);
static uint32 cluster_to_sector(uint32 cluster);
static void to_fat_name(char *src, char *dst);

void FS_CODE fs_init() {
    init_lock(&file_pool_lock, "file_pool_lock");
    init_lock(&fs_lock, "fs_lock");
    
    // 简化：只标记未使用
    for(int i = 0; i < 64; i++) {
        file_pool[i].used = 0;
    }

    accquire_lock(&fs_lock);
    if (virtio_disk_init() < 0) panic("fs_init: disk init failed");
    
    // 分配全局缓存页，用于目录解析等临时操作
    if ((fs.cache_page = (uint8*)kalloc()) == 0) panic("fs_init: kalloc failed");

    if (disk_read(0, fs.cache_page, 1) < 0) panic("fs_init: disk_read(0) failed");

    FAT32_BPB *bpb = (FAT32_BPB*)fs.cache_page;
    // 基础参数计算
    fs.fat_sector = bpb->reserved_sectors;
    fs.fat_size = bpb->fat_size_32;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.root_cluster = bpb->root_cluster;
    fs.first_data_sector = bpb->reserved_sectors + (bpb->fat_count * bpb->fat_size_32);

    printf("RVDOS: FAT32 Initialized. Root @ %d\n", fs.root_cluster);
    release_lock(&fs_lock);
}

uint32 FS_CODE fs_get_root_cluster(void) {
    return fs.root_cluster;
}

static uint32 FS_CODE cluster_to_sector(uint32 cluster) {
    return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}

static uint32 FS_CODE get_next_cluster(uint32 cluster) {
    uint32 fat_offset = cluster * 4;
    uint32 fat_sector = fs.fat_sector + (fat_offset / SECTOR_SIZE);
    uint32 fat_entry_offset = fat_offset % SECTOR_SIZE;
    
    uint8 buf[SECTOR_SIZE];
    if (disk_read(fat_sector, buf, 1) < 0) return 0x0FFFFFFF;

    return (*(uint32*)&buf[fat_entry_offset]) & 0x0FFFFFFF;
}

static int FS_CODE set_next_cluster(uint32 cluster, uint32 next) {
    uint32 fat_offset = cluster * 4;
    uint32 fat_sector = fs.fat_sector + (fat_offset / SECTOR_SIZE);
    uint32 fat_entry_offset = fat_offset % SECTOR_SIZE;
    
    uint8 buf[SECTOR_SIZE];
    if (disk_read(fat_sector, buf, 1) < 0) return -1;
    *(uint32*)&buf[fat_entry_offset] = next;
    return disk_write(fat_sector, buf, 1);
}

static uint32 FS_CODE alloc_cluster() {
    uint8 buf[SECTOR_SIZE];
    for (uint32 s = 0; s < fs.fat_size; s++) {
        if (disk_read(fs.fat_sector + s, buf, 1) < 0) return 0;
        for (int i = 0; i < SECTOR_SIZE; i += 4) {
            uint32 val = (*(uint32*)&buf[i]) & 0x0FFFFFFF;
            if (val == 0) {
                uint32 cluster = (s * SECTOR_SIZE + i) / 4;
                if (cluster < 2) continue;
                *(uint32*)&buf[i] = 0x0FFFFFFF; // Mark as EOC
                disk_write(fs.fat_sector + s, buf, 1);
                
                // Clear the allocated cluster
                uint32 sector = cluster_to_sector(cluster);
                memset(buf, 0, SECTOR_SIZE);
                for (int j = 0; j < fs.sectors_per_cluster; j++) {
                    disk_write(sector + j, buf, 1);
                }
                
                return cluster;
            }
        }
    }
    return 0;
}

// Windows style helpers
file_t* FS_CODE file_alloc() {
    accquire_lock(&file_pool_lock);
    for(int i = 0; i < 64; i++) {
        if(!file_pool[i].used) {
            file_pool[i].used = 1;
            file_pool[i].offset = 0;
            file_pool[i].first_cluster = 0;
            file_pool[i].file_size = 0; // 确保大小也清零
            // 此时不再操作 file_pool[i].data
            release_lock(&file_pool_lock);
            return &file_pool[i];
        }
    }
    release_lock(&file_pool_lock);
    return 0;
}
void FS_CODE file_free(file_t *f) {
    accquire_lock(&file_pool_lock);
    f->used = 0;
    release_lock(&file_pool_lock);
}

// Convert "filename.ext" to FAT32 8.3 format "FILENAMEEXT"
static void FS_CODE to_fat_name(char *src, char *dst) {
    if (strcmp(src, ".") == 0) {
        memcpy(dst, ".          ", 11);
        return;
    }
    if (strcmp(src, "..") == 0) {
        memcpy(dst, "..         ", 11);
        return;
    }
    memset(dst, ' ', 11);
    int i = 0, j = 0;
    while(src[i] && j < 8 && src[i] != '.') {
        char c = src[i++];
        if(c >= 'a' && c <= 'z') c -= 32;
        dst[j++] = c;
    }
    while(src[i] && src[i] != '.') i++;
    if(src[i] == '.') {
        i++;
        j = 8;
        while(src[i] && j < 11) {
            char c = src[i++];
            if(c >= 'a' && c <= 'z') c -= 32;
            dst[j++] = c;
        }
    }
}

static void FS_CODE clear_cluster_chain(uint32 cluster) {
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32 next = get_next_cluster(cluster);
        // Mark cluster as free in FAT
        uint32 fat_offset = cluster * 4;
        uint32 fat_sector = fs.fat_sector + (fat_offset / SECTOR_SIZE);
        uint32 fat_entry_offset = fat_offset % SECTOR_SIZE;
        uint8 buf[SECTOR_SIZE];
        if (disk_read(fat_sector, buf, 1) >= 0) {
            *(uint32*)&buf[fat_entry_offset] = 0;
            disk_write(fat_sector, buf, 1);
        }
        cluster = next;
    }
}

#define O_RDONLY           0
#define O_WRONLY           1
#define O_RDWR             2
#define O_CREATE           0x100
#define O_TRUNC            0x200

// Helper to find a directory entry
static int FS_CODE find_dir_entry(uint32 dir_cluster, char *fat_name, FAT32_DirEntry *out_entry, uint32 *out_sector, int *out_idx) {
    uint32 curr_cluster = dir_cluster;
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
        uint32 sector = cluster_to_sector(curr_cluster);
        for (int s = 0; s < fs.sectors_per_cluster; s++) {
            if (disk_read(sector + s, fs.cache_page, 1) < 0) break;

            FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
            for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
                if (entry[i].name[0] == 0) return -1; // End of directory
                if (entry[i].name[0] == 0xE5) continue; // Deleted entry
                if (entry[i].attr == ATTR_LONG_NAME) continue;

                if (memcmp(entry[i].name, fat_name, 11) == 0) {
                    if (out_entry) *out_entry = entry[i];
                    if (out_sector) *out_sector = sector + s;
                    if (out_idx) *out_idx = i;
                    return 0;
                }
            }
        }
        curr_cluster = get_next_cluster(curr_cluster);
    }
    return -1;
}

// Helper to find a free directory entry
static int FS_CODE find_free_dir_entry(uint32 dir_cluster, uint32 *out_sector, int *out_idx) {
    uint32 curr_cluster = dir_cluster;
    uint32 last_cluster = dir_cluster;
    
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
        uint32 sector = cluster_to_sector(curr_cluster);
        for (int s = 0; s < fs.sectors_per_cluster; s++) {
            if (disk_read(sector + s, fs.cache_page, 1) < 0) break;

            FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
            for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
                if (entry[i].name[0] == 0 || entry[i].name[0] == 0xE5) {
                    *out_sector = sector + s;
                    *out_idx = i;
                    return 0;
                }
            }
        }
        last_cluster = curr_cluster;
        curr_cluster = get_next_cluster(curr_cluster);
    }
    
    // If no free entry found, allocate a new cluster for the directory
    uint32 next = alloc_cluster();
    if (next == 0) return -1;
    set_next_cluster(last_cluster, next);
    
    *out_sector = cluster_to_sector(next);
    *out_idx = 0;
    return 0;
}

int FS_CODE CreateHandler(char *path, int mode) {
    char fat_name[11];
    to_fat_name(path, fat_name);

    accquire_lock(&fs_lock);
    PCB *p = myproc();
    uint32 cwd = p->cwd_cluster;

    FAT32_DirEntry entry;
    uint32 sector;
    int idx;

    if (find_dir_entry(cwd, fat_name, &entry, &sector, &idx) == 0) {
        // Found existing entry
        if (entry.attr & ATTR_DIRECTORY) {
            release_lock(&fs_lock);
            return -1; // Cannot open directory as file
        }

        uint32 first_cluster = (entry.first_cluster_hi << 16) | entry.first_cluster_lo;
        uint32 file_size = entry.file_size;

        if (mode & O_TRUNC) {
            if (first_cluster >= 2) {
                uint32 next = get_next_cluster(first_cluster);
                set_next_cluster(first_cluster, 0x0FFFFFFF); 
                clear_cluster_chain(next);
            } else {
                first_cluster = alloc_cluster();
                disk_read(sector, fs.cache_page, 1);
                FAT32_DirEntry *e = (FAT32_DirEntry*)fs.cache_page;
                e[idx].first_cluster_hi = (first_cluster >> 16) & 0xFFFF;
                e[idx].first_cluster_lo = first_cluster & 0xFFFF;
                disk_write(sector, fs.cache_page, 1);
            }
            file_size = 0;
            disk_read(sector, fs.cache_page, 1);
            FAT32_DirEntry *e = (FAT32_DirEntry*)fs.cache_page;
            e[idx].file_size = 0;
            disk_write(sector, fs.cache_page, 1);
        }

        file_t *f = file_alloc();
        if(!f) { release_lock(&fs_lock); return -1; }
        f->first_cluster = first_cluster;
        f->file_size = file_size;
        f->readable = 1;
        f->writable = 1; 
        f->offset = 0;
        memcpy(f->name, path, 16);
        
        for(int h = 0; h < MAX_HANDLES; h++) {
            if(p->handles[h] == 0) {
                p->handles[h] = f;
                release_lock(&fs_lock);
                return h; 
            }
        }
        file_free(f);
        release_lock(&fs_lock);
        return -1;
    }
    
    // Create new file
    if (mode & O_CREATE) {
        if (find_free_dir_entry(cwd, &sector, &idx) == 0) {
            uint32 cluster = alloc_cluster();
            if (cluster != 0) {
                disk_read(sector, fs.cache_page, 1);
                FAT32_DirEntry *e = (FAT32_DirEntry*)fs.cache_page;
                memset(&e[idx], 0, sizeof(FAT32_DirEntry));
                memcpy(e[idx].name, fat_name, 11);
                e[idx].first_cluster_hi = (cluster >> 16) & 0xFFFF;
                e[idx].first_cluster_lo = cluster & 0xFFFF;
                e[idx].file_size = 0;
                e[idx].attr = ATTR_ARCHIVE;
                disk_write(sector, fs.cache_page, 1);
                
                file_t *f = file_alloc();
                if(f) {
                    f->first_cluster = cluster;
                    f->file_size = 0;
                    f->readable = 1;
                    f->writable = 1;
                    f->offset = 0;
                    memcpy(f->name, path, 16);
                    for(int h = 0; h < MAX_HANDLES; h++) {
                        if(p->handles[h] == 0) {
                            p->handles[h] = f;
                            release_lock(&fs_lock);
                            return h;
                        }
                    }
                    file_free(f);
                }
            }
        }
    }

    release_lock(&fs_lock);
    return -1;
}

int FS_CODE ReadFile(int handle, uint8 *buf, uint32 len) {
    PCB *p = myproc();
    if(handle < 0 || handle >= MAX_HANDLES || !p->handles[handle]) return -1;
    file_t *f = p->handles[handle];
    if(!f->readable) return 0;

    accquire_lock(&fs_lock);
    if(f->offset >= f->file_size) { release_lock(&fs_lock); return 0; }
    if(f->offset + len > f->file_size) len = f->file_size - f->offset;

    uint32 bytes_read = 0;
    uint32 cluster = f->first_cluster;
    uint32 clusters_to_skip = f->offset / (fs.sectors_per_cluster * SECTOR_SIZE);
    for(uint32 i = 0; i < clusters_to_skip; i++) {
        cluster = get_next_cluster(cluster);
        if(cluster >= 0x0FFFFFF8) { release_lock(&fs_lock); return 0; }
    }

    uint32 offset_in_cluster = f->offset % (fs.sectors_per_cluster * SECTOR_SIZE);
    while(bytes_read < len && cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32 sector_base = cluster_to_sector(cluster);
        for(int s = offset_in_cluster / SECTOR_SIZE; s < fs.sectors_per_cluster && bytes_read < len; s++) {
            uint32 skip = offset_in_cluster % SECTOR_SIZE;
            uint32 can_read = SECTOR_SIZE - skip;
            if(can_read > (len - bytes_read)) can_read = len - bytes_read;

            if(disk_read(sector_base + s, fs.cache_page, 1) < 0) {
                f->offset += bytes_read;
                release_lock(&fs_lock);
                return bytes_read;
            }
            memcpy(buf + bytes_read, fs.cache_page + skip, can_read);
            bytes_read += can_read;
            offset_in_cluster = 0; 
        }
        cluster = get_next_cluster(cluster);
        offset_in_cluster = 0; 
    }

    f->offset += bytes_read;
    release_lock(&fs_lock);
    return bytes_read;
}

static void FS_CODE update_dir_entry_info_cwd(uint32 cwd, char *path, uint32 cluster, uint32 new_size) {
    char fat_name[11];
    to_fat_name(path, fat_name);

    uint32 curr_cluster = cwd;
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
        uint32 sector = cluster_to_sector(curr_cluster);
        for (int s = 0; s < fs.sectors_per_cluster; s++) {
            if (disk_read(sector + s, fs.cache_page, 1) < 0) break;

            FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
            for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
                if (entry[i].name[0] == 0) return; 
                
                if (memcmp(entry[i].name, fat_name, 11) == 0) {
                    if (cluster != 0) {
                        entry[i].first_cluster_hi = (cluster >> 16) & 0xFFFF;
                        entry[i].first_cluster_lo = cluster & 0xFFFF;
                    }
                    if (new_size != 0xFFFFFFFF) entry[i].file_size = new_size;
                    disk_write(sector + s, fs.cache_page, 1);
                    return;
                }
            }
        }
        curr_cluster = get_next_cluster(curr_cluster);
    }
}

int FS_CODE WriteFile(int handle, uint8 *buf, uint32 len) {
    PCB *p = myproc();
    if(handle < 0 || handle >= MAX_HANDLES || !p->handles[handle]) return -1;
    
    if (p->handles[handle] == (file_t *)-1) {
        extern spinlock_t uart_lock;
        extern void uart_putc_no_lock(char c);
        accquire_lock(&uart_lock);
        for (uint32 i = 0; i < len; i++) uart_putc_no_lock(buf[i]);
        release_lock(&uart_lock);
        return len;
    }

    file_t *f = p->handles[handle];
    if(!f->writable) return -1;

    accquire_lock(&fs_lock);
    uint32 bytes_written = 0;

    if (f->first_cluster == 0) {
        uint32 cluster = alloc_cluster();
        if (cluster == 0) { release_lock(&fs_lock); return -1; }
        f->first_cluster = cluster;
        update_dir_entry_info_cwd(p->cwd_cluster, f->name, cluster, 0xFFFFFFFF);
    }

    uint32 cluster = f->first_cluster;
    uint32 prev_cluster = 0;
    uint32 clusters_to_skip = f->offset / (fs.sectors_per_cluster * SECTOR_SIZE);
    for(uint32 i = 0; i < clusters_to_skip; i++) {
        prev_cluster = cluster;
        cluster = get_next_cluster(cluster);
        if(cluster >= 0x0FFFFFF8) {
            uint32 next = alloc_cluster();
            if (next == 0) { release_lock(&fs_lock); return bytes_written; }
            set_next_cluster(prev_cluster, next);
            cluster = next;
        }
    }

    uint32 offset_in_cluster = f->offset % (fs.sectors_per_cluster * SECTOR_SIZE);
    while(bytes_written < len && cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32 sector_start = cluster_to_sector(cluster);
        for(int s = offset_in_cluster / SECTOR_SIZE; s < fs.sectors_per_cluster && bytes_written < len; s++) {
            uint32 skip = offset_in_cluster % SECTOR_SIZE;
            uint32 can_write = SECTOR_SIZE - skip;
            if(can_write > (len - bytes_written)) can_write = len - bytes_written;

            if (skip != 0 || can_write < SECTOR_SIZE) {
                disk_read(sector_start + s, fs.cache_page, 1);
                memcpy(fs.cache_page + skip, buf + bytes_written, can_write);
                disk_write(sector_start + s, fs.cache_page, 1);
            } else {
                disk_write(sector_start + s, buf + bytes_written, 1);
            }
            bytes_written += can_write;
            offset_in_cluster = 0; 
        }

        if (bytes_written < len) {
            prev_cluster = cluster;
            cluster = get_next_cluster(cluster);
            if (cluster >= 0x0FFFFFF8) {
                uint32 next = alloc_cluster();
                if (next == 0) break;
                set_next_cluster(prev_cluster, next);
                cluster = next;
            }
            offset_in_cluster = 0;
        }
    }

    f->offset += bytes_written;
    if (f->offset > f->file_size) {
        f->file_size = f->offset;
        update_dir_entry_info_cwd(p->cwd_cluster, f->name, 0, f->file_size);
    }

    release_lock(&fs_lock);
    return bytes_written;
}

void FS_CODE CloseHandle(int handle) {
    PCB *p = myproc();
    if(handle < 0 || handle >= MAX_HANDLES || p->handles[handle] == 0) return;
    if (p->handles[handle] == (file_t *)-1) { p->handles[handle] = 0; return; }
    file_t *f = p->handles[handle];
    file_free(f);
    p->handles[handle] = 0;
}

void FS_CODE fs_ls() {
    uint8 sector_buf[SECTOR_SIZE];
    PCB *p = myproc();
    uint32 curr_cluster = p->cwd_cluster;

    accquire_lock(&fs_lock);
    while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
        uint32 sector_base = cluster_to_sector(curr_cluster);
        for (int s = 0; s < fs.sectors_per_cluster; s++) {
            if (disk_read(sector_base + s, sector_buf, 1) < 0) break;

            FAT32_DirEntry *entry = (FAT32_DirEntry*)sector_buf;
            for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
                if (entry[i].name[0] == 0) goto done;
                if (entry[i].name[0] == 0xE5) continue;
                if (entry[i].attr == ATTR_LONG_NAME) continue;

                char buf[32];
                int p_idx = 0;
                if (entry[i].attr & ATTR_DIRECTORY) buf[p_idx++] = '[';
                for (int j = 0; j < 8; j++) if (entry[i].name[j] != ' ') buf[p_idx++] = entry[i].name[j];
                if (entry[i].ext[0] != ' ') {
                    buf[p_idx++] = '.';
                    for (int j = 0; j < 3; j++) if (entry[i].ext[j] != ' ') buf[p_idx++] = entry[i].ext[j];
                }
                if (entry[i].attr & ATTR_DIRECTORY) buf[p_idx++] = ']';
                buf[p_idx++] = ' '; buf[p_idx++] = ' '; buf[p_idx] = '\0';
                WriteFile(STDOUT, (uint8*)buf, strlen(buf));

                char sz_buf[16];
                uint32 sz = entry[i].file_size;
                int k = 0;
                if (sz == 0) sz_buf[k++] = '0';
                else {
                    char temp[16];
                    int l = 0;
                    while (sz > 0) { temp[l++] = (sz % 10) + '0'; sz /= 10; }
                    while (l > 0) sz_buf[k++] = temp[--l];
                }
                sz_buf[k++] = ' '; sz_buf[k++] = 'b'; sz_buf[k++] = 'y'; sz_buf[k++] = 't'; sz_buf[k++] = 'e'; sz_buf[k++] = 's'; sz_buf[k++] = '\n'; sz_buf[k] = '\0';
                WriteFile(STDOUT, (uint8*)sz_buf, strlen(sz_buf));
            }
        }
        curr_cluster = get_next_cluster(curr_cluster);
    }
done:
    release_lock(&fs_lock);
}

int FS_CODE fs_read_file_offset(char *filename, uint8 *out_buf, uint32 offset, uint32 max_len) {
    char fat_name[11];
    to_fat_name(filename, fat_name);

    accquire_lock(&fs_lock);
    PCB *p = myproc();
    uint32 cwd = p ? p->cwd_cluster : fs.root_cluster;

    FAT32_DirEntry target;
    if (find_dir_entry(cwd, fat_name, &target, 0, 0) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    if (offset >= target.file_size) { release_lock(&fs_lock); return 0; }
    uint32 size = target.file_size - offset;
    if (size > max_len) size = max_len;

    uint32 cluster = (target.first_cluster_hi << 16) | target.first_cluster_lo;
    uint32 bytes_to_skip = offset;
    while (bytes_to_skip >= fs.sectors_per_cluster * SECTOR_SIZE) {
        cluster = get_next_cluster(cluster);
        if (cluster >= 0x0FFFFFF8) { release_lock(&fs_lock); return 0; }
        bytes_to_skip -= fs.sectors_per_cluster * SECTOR_SIZE;
    }

    uint32 bytes_read = 0;
    uint32 offset_in_cluster = bytes_to_skip;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && bytes_read < size) {
        uint32 sector = cluster_to_sector(cluster);
        for (int s = offset_in_cluster / SECTOR_SIZE; s < fs.sectors_per_cluster && bytes_read < size; s++) {
            uint32 skip_in_sector = offset_in_cluster % SECTOR_SIZE;
            uint32 can_read = SECTOR_SIZE - skip_in_sector;
            if (can_read > (size - bytes_read)) can_read = size - bytes_read;

            if (disk_read(sector + s, fs.cache_page, 1) < 0) {
                release_lock(&fs_lock);
                return bytes_read;
            }
            memcpy(out_buf + bytes_read, fs.cache_page + skip_in_sector, can_read);
            bytes_read += can_read;
            offset_in_cluster = 0;
        }
        cluster = get_next_cluster(cluster);
        offset_in_cluster = 0;
    }

    release_lock(&fs_lock);
    return bytes_read;
}

// Internal version of read_file that doesn't use handles
int FS_CODE fs_read_file(char *filename, uint8 *out_buf, uint32 max_len) {
    return fs_read_file_offset(filename, out_buf, 0, max_len);
}

int FS_CODE MakeDir(char *path) {
    char fat_name[11];
    to_fat_name(path, fat_name);

    accquire_lock(&fs_lock);
    PCB *p = myproc();
    uint32 cwd = p->cwd_cluster;

    if (find_dir_entry(cwd, fat_name, 0, 0, 0) == 0) {
        release_lock(&fs_lock);
        return -1; // Already exists
    }

    uint32 sector;
    int idx;
    if (find_free_dir_entry(cwd, &sector, &idx) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    uint32 cluster = alloc_cluster();
    if (cluster == 0) { release_lock(&fs_lock); return -1; }

    // Write directory entry in parent
    disk_read(sector, fs.cache_page, 1);
    FAT32_DirEntry *e = (FAT32_DirEntry*)fs.cache_page;
    memset(&e[idx], 0, sizeof(FAT32_DirEntry));
    memcpy(e[idx].name, fat_name, 11);
    e[idx].first_cluster_hi = (cluster >> 16) & 0xFFFF;
    e[idx].first_cluster_lo = cluster & 0xFFFF;
    e[idx].attr = ATTR_DIRECTORY;
    disk_write(sector, fs.cache_page, 1);

    // Initialize new directory with . and ..
    uint32 new_sector = cluster_to_sector(cluster);
    memset(fs.cache_page, 0, SECTOR_SIZE);
    FAT32_DirEntry *dot = (FAT32_DirEntry*)fs.cache_page;
    
    memcpy(dot[0].name, ".          ", 11);
    dot[0].attr = ATTR_DIRECTORY;
    dot[0].first_cluster_hi = (cluster >> 16) & 0xFFFF;
    dot[0].first_cluster_lo = cluster & 0xFFFF;

    memcpy(dot[1].name, "..         ", 11);
    dot[1].attr = ATTR_DIRECTORY;
    uint32 parent = (cwd == fs.root_cluster) ? 0 : cwd; // FAT32: .. of root or subdir to root is 0
    dot[1].first_cluster_hi = (parent >> 16) & 0xFFFF;
    dot[1].first_cluster_lo = parent & 0xFFFF;

    disk_write(new_sector, fs.cache_page, 1);

    release_lock(&fs_lock);
    return 0;
}

int FS_CODE ChangeDir(char *path) {
    PCB *p = myproc();
    if (strcmp(path, "/") == 0) {
        p->cwd_cluster = fs.root_cluster;
        p->cwd_path[0] = '/';
        p->cwd_path[1] = '\0';
        return 0;
    }

    char fat_name[11];
    to_fat_name(path, fat_name);

    accquire_lock(&fs_lock);
    FAT32_DirEntry entry;
    if (find_dir_entry(p->cwd_cluster, fat_name, &entry, 0, 0) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    if (!(entry.attr & ATTR_DIRECTORY)) {
        release_lock(&fs_lock);
        return -1;
    }

    uint32 cluster = (entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    if (cluster == 0) cluster = fs.root_cluster;
    p->cwd_cluster = cluster;

    // Update cwd_path
    if (strcmp(path, ".") == 0) {
        // No change
    } else if (strcmp(path, "..") == 0) {
        if (strcmp(p->cwd_path, "/") != 0) {
            int len = strlen(p->cwd_path);
            int i = len - 1;
            while (i > 0 && p->cwd_path[i] != '/') i--;
            if (i == 0) p->cwd_path[1] = '\0';
            else p->cwd_path[i] = '\0';
        }
    } else {
        int len = strlen(p->cwd_path);
        if (len > 1) {
            p->cwd_path[len++] = '/';
        }
        int i = 0;
        while (path[i] && len < 127) {
            p->cwd_path[len++] = path[i++];
        }
        p->cwd_path[len] = '\0';
    }

    release_lock(&fs_lock);
    return 0;
}

int FS_CODE Unlink(char *path) {
    char fat_name[11];
    to_fat_name(path, fat_name);

    accquire_lock(&fs_lock);
    FAT32_DirEntry entry;
    uint32 sector;
    int idx;
    if (find_dir_entry(myproc()->cwd_cluster, fat_name, &entry, &sector, &idx) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    if (entry.attr & ATTR_DIRECTORY) {
        // Check if directory is empty (only . and ..)
        uint32 cluster = (entry.first_cluster_hi << 16) | entry.first_cluster_lo;
        uint32 curr_cluster = cluster;
        while (curr_cluster >= 2 && curr_cluster < 0x0FFFFFF8) {
            uint32 s_base = cluster_to_sector(curr_cluster);
            for (int s = 0; s < fs.sectors_per_cluster; s++) {
                uint8 buf[SECTOR_SIZE];
                disk_read(s_base + s, buf, 1);
                FAT32_DirEntry *e = (FAT32_DirEntry*)buf;
                for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
                    if (e[i].name[0] == 0) goto empty_check_done;
                    if (e[i].name[0] == 0xE5) continue;
                    if (memcmp(e[i].name, ".          ", 11) == 0) continue;
                    if (memcmp(e[i].name, "..         ", 11) == 0) continue;
                    // Found something else
                    release_lock(&fs_lock);
                    return -1; 
                }
            }
            curr_cluster = get_next_cluster(curr_cluster);
        }
    }
empty_check_done:

    // Free clusters
    uint32 first_cluster = (entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    clear_cluster_chain(first_cluster);

    // Remove directory entry
    disk_read(sector, fs.cache_page, 1);
    FAT32_DirEntry *e = (FAT32_DirEntry*)fs.cache_page;
    e[idx].name[0] = 0xE5;
    disk_write(sector, fs.cache_page, 1);

    release_lock(&fs_lock);
    return 0;
}

int FS_CODE disk_write(uint32 sector, uint8 *buf, uint32 count) {
  disk.status[0] = 0xff; 
  disk.ops[0].type = VIRTIO_BLK_T_OUT;
  disk.ops[0].reserved = 0;
  disk.ops[0].sector = sector;

  disk.desc[0].addr = (uint64)&disk.ops[0];
  disk.desc[0].len = sizeof(struct virtio_blk_req);
  disk.desc[0].flags = VIRTQ_DESC_F_NEXT;
  disk.desc[0].next = 1;

  disk.desc[1].addr = (uint64)buf;
  disk.desc[1].len = 512 * count;
  disk.desc[1].flags = VIRTQ_DESC_F_NEXT;
  disk.desc[1].next = 2;

  disk.desc[2].addr = (uint64)&disk.status[0];
  disk.desc[2].len = 1;
  disk.desc[2].flags = VIRTQ_DESC_F_WRITE;
  disk.desc[2].next = 0;

  uint16 old_idx = disk.avail->idx;
  disk.avail->ring[old_idx % 16] = 0;
  
  __sync_synchronize();
  disk.avail->idx += 1;
  __sync_synchronize();

  *REG_V(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; 

  uint64 timeout = 1000000;
  while(disk.status[0] == 0xff && timeout > 0) { timeout--; }

  if(disk.status[0] != 0) return -1;
  return 0;
}
