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

void FS_CODE fs_init() {
    init_lock(&file_pool_lock, "file_pool_lock");
    init_lock(&fs_lock, "fs_lock");
    for(int i = 0; i < 64; i++) file_pool[i].used = 0;

    accquire_lock(&fs_lock);
    // 1. 如果磁盘驱动初始化失败，严禁执行后续 disk_read
    if (virtio_disk_init() < 0) {
        panic("fs_init: disk init failed");
    }
    
    fs.cache_page = (uint8*)kalloc();
    if (fs.cache_page == 0) {
        panic("fs_init: kalloc cache failed");
    }

    // 2. 尝试读取引导扇区
    if (disk_read(0, fs.cache_page, 1) < 0) {
        panic("fs_init: disk_read(0) failed! Disk image might be missing.");
    }

    FAT32_BPB *bpb = (FAT32_BPB*)fs.cache_page;
    if (bpb->bytes_per_sector != SECTOR_SIZE) {
        panic("fs_init: Invalid FAT32 BPB");
    }

    // 3. 计算参数
    fs.fat_sector = bpb->reserved_sectors;
    fs.fat_size = bpb->fat_size_32;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.root_cluster = bpb->root_cluster;
    fs.first_data_sector = bpb->reserved_sectors + (bpb->fat_count * bpb->fat_size_32);

    printf("fs_init: FAT32 ready. Root Cluster: %d\n", fs.root_cluster);
    release_lock(&fs_lock);
}

static uint32 FS_CODE cluster_to_sector(uint32 cluster) {
    return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}

static uint32 FS_CODE get_next_cluster(uint32 cluster) {
    uint32 fat_offset = cluster * 4;
    uint32 fat_sector = fs.fat_sector + (fat_offset / SECTOR_SIZE);
    uint32 fat_entry_offset = fat_offset % SECTOR_SIZE;
    
    uint8 buf[SECTOR_SIZE];
    // Note: Called while holding fs_lock in higher level functions
    if (disk_read(fat_sector, buf, 1) < 0) return 0x0FFFFFFF;

    return (*(uint32*)&buf[fat_entry_offset]) & 0x0FFFFFFF;
}

// Windows style helpers
file_t* FS_CODE file_alloc() {
    accquire_lock(&file_pool_lock);
    for(int i = 0; i < 64; i++) {
        if(!file_pool[i].used) {
            file_pool[i].used = 1;
            file_pool[i].offset = 0;
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

int FS_CODE CreateHandler(char *path) {
    char fat_name[11];
    to_fat_name(path, fat_name);

    accquire_lock(&fs_lock);
    uint32 root_sector = cluster_to_sector(fs.root_cluster);
    if (disk_read(root_sector, fs.cache_page, 1) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
    for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
        if (entry[i].name[0] == 0) break;
        if (entry[i].name[0] == 0xE5) continue;
        
        if (memcmp(entry[i].name, fat_name, 11) == 0) {
            file_t *f = file_alloc();
            if(!f) {
                release_lock(&fs_lock);
                return -1;
            }
            f->first_cluster = (entry[i].first_cluster_hi << 16) | entry[i].first_cluster_lo;
            f->file_size = entry[i].file_size;
            f->readable = 1;
            f->writable = 0; // Read-only for now
            
            PCB *p = myproc();
            for(int h = 0; h < MAX_HANDLES; h++) {
                if(p->handles[h] == 0) {
                    p->handles[h] = f;
                    release_lock(&fs_lock);
                    return h; // Return HANDLE
                }
            }
            file_free(f);
            release_lock(&fs_lock);
            return -1;
        }
    }
    release_lock(&fs_lock);
    return -1;
}

int FS_CODE ReadFile(int handle, uint8 *buf, uint32 len) {
    PCB *p = myproc();
    if(handle < 0 || handle >= MAX_HANDLES || !p->handles[handle]) return -1;
    file_t *f = p->handles[handle];

    accquire_lock(&fs_lock);
    if(f->offset >= f->file_size) {
        release_lock(&fs_lock);
        return 0;
    }
    if(f->offset + len > f->file_size) len = f->file_size - f->offset;

    uint32 bytes_read = 0;
    uint32 cluster = f->first_cluster;
    
    // Skip to current offset cluster
    uint32 clusters_to_skip = f->offset / (fs.sectors_per_cluster * SECTOR_SIZE);
    for(uint32 i = 0; i < clusters_to_skip; i++) {
        cluster = get_next_cluster(cluster);
        if(cluster >= 0x0FFFFFF8) {
            release_lock(&fs_lock);
            return 0;
        }
    }

    uint32 offset_in_cluster = f->offset % (fs.sectors_per_cluster * SECTOR_SIZE);
    
    while(bytes_read < len && cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32 sector = cluster_to_sector(cluster);
        for(int s = offset_in_cluster / SECTOR_SIZE; s < fs.sectors_per_cluster && bytes_read < len; s++) {
            uint32 skip_in_sector = offset_in_cluster % SECTOR_SIZE;
            uint32 can_read = SECTOR_SIZE - skip_in_sector;
            if(can_read > (len - bytes_read)) can_read = len - bytes_read;

            uint8 temp[SECTOR_SIZE];
            if(disk_read(sector + s, temp, 1) < 0) {
                f->offset += bytes_read;
                release_lock(&fs_lock);
                return bytes_read;
            }
            memcpy(buf + bytes_read, temp + skip_in_sector, can_read);
            
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

// ... existing CloseHandle function ...
void FS_CODE CloseHandle(int handle) {
    PCB *p = myproc();
    if(handle >= 0 && handle < MAX_HANDLES && p->handles[handle]) {
        if (p->handles[handle] != (file_t *)-1) {
            file_free(p->handles[handle]);
        }
        p->handles[handle] = 0;
    }
}

// --- Compatibility Wrappers for Kernel Internal Use ---

void FS_CODE fs_ls() {
    accquire_lock(&fs_lock);
    uint32 root_sector = cluster_to_sector(fs.root_cluster);
    if (disk_read(root_sector, fs.cache_page, 1) < 0) {
        release_lock(&fs_lock);
        return;
    }

    FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
    for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
        if (entry[i].name[0] == 0) break;
        if (entry[i].name[0] == 0xE5) continue;
        if (entry[i].attr == 0x0F) continue;

        for (int j = 0; j < 8; j++) if (entry[i].name[j] != ' ') uart_putc(entry[i].name[j]);
        if (entry[i].ext[0] != ' ') {
            uart_putc('.');
            for (int j = 0; j < 3; j++) if (entry[i].ext[j] != ' ') uart_putc(entry[i].ext[j]);
        }
        printf("  %d bytes\n", entry[i].file_size);
    }
    release_lock(&fs_lock);
}

// Internal version of read_file that doesn't use handles
int FS_CODE fs_read_file(char *filename, uint8 *out_buf, uint32 max_len) {
    return fs_read_file_offset(filename, out_buf, 0, max_len);
}

int FS_CODE fs_read_file_offset(char *filename, uint8 *out_buf, uint32 offset, uint32 max_len) {
    char fat_name[11];
    to_fat_name(filename, fat_name);

    accquire_lock(&fs_lock);
    uint32 root_sector = cluster_to_sector(fs.root_cluster);
    if (disk_read(root_sector, fs.cache_page, 1) < 0) {
        release_lock(&fs_lock);
        return -1;
    }

    FAT32_DirEntry *entry = (FAT32_DirEntry*)fs.cache_page;
    FAT32_DirEntry target;
    int found = 0;

    for (int i = 0; i < SECTOR_SIZE / sizeof(FAT32_DirEntry); i++) {
        if (entry[i].name[0] == 0) break;
        if (entry[i].name[0] == 0xE5) continue;
        
        if (memcmp(entry[i].name, fat_name, 11) == 0) {
            target = entry[i];
            found = 1;
            break;
        }
    }

    if (!found) {
        release_lock(&fs_lock);
        return -1;
    }

    if (offset >= target.file_size) {
        release_lock(&fs_lock);
        return 0;
    }

    uint32 size = target.file_size - offset;
    if (size > max_len) size = max_len;

    uint32 cluster = (target.first_cluster_hi << 16) | target.first_cluster_lo;
    
    // Skip to offset
    uint32 bytes_to_skip = offset;
    while (bytes_to_skip >= fs.sectors_per_cluster * SECTOR_SIZE) {
        cluster = get_next_cluster(cluster);
        if (cluster >= 0x0FFFFFF8) {
            release_lock(&fs_lock);
            return 0;
        }
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

            uint8 temp[SECTOR_SIZE];
            if (disk_read(sector + s, temp, 1) < 0) {
                release_lock(&fs_lock);
                return bytes_read;
            }
            memcpy(out_buf + bytes_read, temp + skip_in_sector, can_read);
            
            bytes_read += can_read;
            offset_in_cluster = 0;
        }
        cluster = get_next_cluster(cluster);
        offset_in_cluster = 0;
    }

    release_lock(&fs_lock);
    return bytes_read;
}
