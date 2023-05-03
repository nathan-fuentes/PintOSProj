#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

int clock_hand = 0; /* Used for clock algorithm (may need to initialize in init.c) */

void update_bits(block_sector_t sector_number, int i, bool write);
int clock_algorithm(void);
// void find_idx(struct inode_disk* id, int* direct_idx, int* indirect_idx, off_t offset);
// bool inode_resize(struct inode_disk* id, off_t size);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  // block_sector_t direct[12];        /* Direct pointers */
  // block_sector_t indirect;          /* Indirect pointer */
  // block_sector_t doubly_indirect;   /* Doubly Indirect pointer */
  // off_t length;                     /* File size in bytes. */
  // off_t is_directory;               /* (Used in next section) Denotes if this inode is for a directory or not */
  // unsigned magic;                   /* Magic number. */
  // uint32_t unused[111];             /* Not used. */

  block_sector_t start; /* First data sector. */
  off_t length;         /* File size in bytes. */
  unsigned magic;       /* Magic number. */
  uint32_t unused[125]; /* Not used. */
};


int clock_algorithm(void) {
  while (true) {
    if (!buffer_cache[clock_hand].clock_bit) {
      int ret = clock_hand;
      clock_hand = (clock_hand + 1) % 64;
      return ret;
    } else {
      buffer_cache[clock_hand].clock_bit = false;
      clock_hand = (clock_hand + 1) % 64;
    }
  }
}

void update_bits(block_sector_t sector_number, int i, bool write) {
  buffer_cache[i].sector_number = sector_number;
  buffer_cache[i].valid_bit = true;
  buffer_cache[i].clock_bit = true;
  buffer_cache[i].dirty_bit = buffer_cache[i].dirty_bit || write;
}

// TODO: Later, implement this function outside of inode.c (wherever block read / write are also called)
// TODO: UNCOMMENT RELEASE / ACQUIRE CACHE LOCK
void cache_function(struct block* block, block_sector_t sector_number, void* buffer, bool write, off_t size, off_t offset) {
  cache_entry_t cache_entry;
  lock_acquire(&cache_lock);
  // if (!write) {
  //   printf("Read %d\n", (int) sector_number);
  // } else if (write) {   
  //   printf("Write %d\n", (int) sector_number);
  // }
  int idx = -1;
  for (int i = 0; i < 64; i++) {
    if (buffer_cache[i].sector_number == sector_number && buffer_cache[i].valid_bit) {
      // printf("Found at index: %d\n", i);
      idx = i;
      update_bits(sector_number, i, write);
      break;
    } else if (!buffer_cache[i].valid_bit) {
      // printf("Placed at Empty index: %d", i);
      idx = i;
      update_bits(sector_number, i, write);
      // lock_release(&cache_lock);
      lock_acquire(&(buffer_cache[i].lock));
      if (sector_number != buffer_cache[i].sector_number) {
        // Entry invalid again, redo logic.
        lock_release(&(buffer_cache[i].lock));
        cache_function(block, sector_number, buffer, write, size, offset);
        return;
      }
      block_read(block, sector_number, &(buffer_cache[i].data));
      lock_release(&(buffer_cache[i].lock));
      // lock_acquire(&cache_lock);
      break;
    }
  }
  
  if (idx == -1) {
    // cache entry not found, do clock a
    idx = clock_algorithm();
    // printf("Evicted entry: %d\n", idx);
    bool old_dirty_bit = buffer_cache[idx].dirty_bit;
    block_sector_t old_sector_number = buffer_cache[idx].sector_number;
    update_bits(sector_number, idx, write);
    // lock_release(&cache_lock);
    lock_acquire(&buffer_cache[idx].lock);
    if (buffer_cache[idx].sector_number != sector_number) {
      // Entry invalid again, redo logic.
      lock_release(&buffer_cache[idx].lock);
      cache_function(block, sector_number, buffer, write, size, offset);
      return;
    }
    if (old_dirty_bit) {
      block_write(block, old_sector_number, &buffer_cache[idx].data);
    }
    block_read(block, sector_number, &buffer_cache[idx].data);
    lock_release(&buffer_cache[idx].lock);
    // lock_acquire(&cache_lock);
  }
  
  // printf("Before Op\n");
  if (write) { // perform a write operation
    memcpy((&(buffer_cache[idx].data)) + offset, buffer, size);
  } else { // perform a read operation
    memcpy(buffer, (&(buffer_cache[idx].data)) + offset, size);
  }
  // printf("After Op\n");
  lock_release(&cache_lock);
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
  struct lock lock;
};

// bool inode_resize(struct inode_disk* id, off_t size) {
//   return true;
// }

// void find_idx(struct inode_disk* id, int* direct_idx, int* indirect_idx, off_t offset) {
//   int block_idx = offset/BLOCK_SECTOR_SIZE;
//   int num_dir_ptrs = 12;
//   int entries_in_ind_ptr = 128;
//   if (block_idx < 12) {
//     *direct_idx = block_idx;
//     *indirect_idx = -2;
//   } else if (block_idx < 140) {
//     *direct_idx = block_idx - 12;
//     *indirect_idx = -1;
//   } else {
//     *indirect_idx = (block_idx - num_dir_ptrs + entries_in_ind_ptr) / 128;
//     *direct_idx = (block_idx - num_dir_ptrs + entries_in_ind_ptr) % 128;
//   }
// }

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  // struct inode_disk* id = calloc(1, sizeof(struct inode_disk));
  // block_read(fs_device, inode->sector, id);
  // int* direct_idx = calloc(1, sizeof(int));
  // int* indirect_idx = calloc(1, sizeof(int));
  // find_idx(id, direct_idx, indirect_idx, pos);
  // if (indirect_idx == -2) {
  //   return id->direct[*direct_idx];
  // } else if (indirect_idx == -1) {
  //   // cache_function()
  //   block_read(fs_device, id->indirect, )
  // }
  
  
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    if (free_map_allocate(sectors, &disk_inode->start)) {
      cache_function(fs_device, sector, disk_inode, true, BLOCK_SECTOR_SIZE, 0);
      // block_write(fs_device, sector, disk_inode);
      if (sectors > 0) {
        static char zeros[BLOCK_SECTOR_SIZE];
        size_t i;

        for (i = 0; i < sectors; i++)
          cache_function(fs_device, disk_inode->start + i, zeros, true, BLOCK_SECTOR_SIZE, 0);
          // block_write(fs_device, disk_inode->start + i, zeros);
      }
      success = true;
    }
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  // block_read(fs_device, inode->sector, &inode->data);
  cache_function(fs_device, inode->sector, &inode->data, false, BLOCK_SECTOR_SIZE, 0);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      free_map_release(inode->data.start, bytes_to_sectors(inode->data.length));
    }

    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Read full sector directly into caller's buffer. */
    //   block_read(fs_device, sector_idx, buffer + bytes_read);
    // } else {
    //   /* Read sector into bounce buffer, then partially copy
    //          into caller's buffer. */
    //   if (bounce == NULL) {
    //     bounce = malloc(BLOCK_SECTOR_SIZE);
    //     if (bounce == NULL)
    //       break;
    //   }
    //   block_read(fs_device, sector_idx, bounce);
    //   memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    // }

    cache_function(fs_device, sector_idx, buffer + bytes_read, false, chunk_size, sector_ofs);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  // free(bounce); // TODO: Don't forget to delete this with bounce

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Write full sector directly to disk. */
    //   block_write(fs_device, sector_idx, buffer + bytes_written);
    // } else {
    //   /* We need a bounce buffer. */
    //   if (bounce == NULL) {
    //     bounce = malloc(BLOCK_SECTOR_SIZE);
    //     if (bounce == NULL)
    //       break;
    //   }

    //   /* If the sector contains data before or after the chunk
    //          we're writing, then we need to read in the sector
    //          first.  Otherwise we start with a sector of all zeros. */
    //   if (sector_ofs > 0 || chunk_size < sector_left)
    //     block_read(fs_device, sector_idx, bounce);
    //   else
    //     memset(bounce, 0, BLOCK_SECTOR_SIZE);
    //   memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
    //   block_write(fs_device, sector_idx, bounce);
    // }
    cache_function(fs_device, sector_idx, buffer + bytes_written, true, chunk_size, sector_ofs);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  // free(bounce); // TODO: Don't forget to delete this with bounce

  return bytes_written;
  
  //Implementation starts here
  
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) { 
  return inode->data.length; // TODO: Remove this line & implement inode_length properly
  // struct inode_disk* disk_inode = NULL;
  // disk_inode = calloc(1, sizeof *disk_inode);
  // cache_function(fs_device, inode->sector, disk_inode, false, BLOCK_SECTOR_SIZE, 0);
  // off_t len = disk_inode->length;
  // free(disk_inode);
  // return len; 
  }
