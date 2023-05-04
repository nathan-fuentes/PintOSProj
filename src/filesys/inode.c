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

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  block_sector_t direct[12];        /* Direct pointers */
  block_sector_t indirect;          /* Indirect pointer */
  block_sector_t doubly_indirect;   /* Doubly Indirect pointer */
  off_t length;                     /* File size in bytes. */
  off_t is_directory;               /* (Used in next section) Denotes if this inode is for a directory or not */
  unsigned magic;                   /* Magic number. */
  uint32_t unused[111];             /* Not used. */

  // block_sector_t start; /* First data sector. */
  // off_t length;         /* File size in bytes. */
  // unsigned magic;       /* Magic number. */
  // uint32_t unused[125]; /* Not used. */
};

void find_idx(struct inode_disk* id, int* direct_idx, int* indirect_idx, off_t offset);
bool inode_resize(struct inode_disk* id, off_t size);


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

// TODO: UNCOMMENT RELEASE / ACQUIRE CACHE LOCK FOR MORE IN DEPTH SYNCH
void cache_function(struct block* block, block_sector_t sector_number, void* buffer, bool write, off_t size, off_t offset) {
  lock_acquire(&cache_lock);
  int idx = -1;
  for (int i = 0; i < 64; i++) {
    if (buffer_cache[i].sector_number == sector_number && buffer_cache[i].valid_bit) {
      idx = i;
      update_bits(sector_number, i, write);
      // lock_release(&cache_lock);
      // lock_acquire(&(buffer_cache[i].lock));
      break;
    } else if (!buffer_cache[i].valid_bit) {
      idx = i;
      update_bits(sector_number, i, write);
      // lock_release(&cache_lock);
      // lock_acquire(&(buffer_cache[i].lock));
      if (sector_number != buffer_cache[i].sector_number) {
        // Entry invalid again, redo logic.
        // lock_release(&(buffer_cache[i].lock));
        cache_function(block, sector_number, buffer, write, size, offset);
        return;
      }
      block_read(block, sector_number, &(buffer_cache[i].data));
      //lock_release(&(buffer_cache[i].lock));
      //lock_acquire(&cache_lock);
      break;
    }
  }
  
  if (idx == -1) {
    // cache entry not found, do clock a
    idx = clock_algorithm();
    bool old_dirty_bit = buffer_cache[idx].dirty_bit;
    block_sector_t old_sector_number = buffer_cache[idx].sector_number;
    update_bits(sector_number, idx, write);
    // lock_release(&cache_lock);
    // lock_acquire(&buffer_cache[idx].lock);
    if (buffer_cache[idx].sector_number != sector_number) {
      // Entry invalid again, redo logic.
      // lock_release(&buffer_cache[idx].lock);
      cache_function(block, sector_number, buffer, write, size, offset);
      return;
    }
    if (old_dirty_bit) {
      block_write(block, old_sector_number, &buffer_cache[idx].data);
    }
    block_read(block, sector_number, &buffer_cache[idx].data);
    // lock_release(&buffer_cache[idx].lock);
    // lock_acquire(&cache_lock);
  }
  
  if (write) { // perform a write operation
    memcpy(buffer_cache[idx].data + offset, buffer, size);
  } else { // perform a read operation
    memcpy(buffer, buffer_cache[idx].data + offset, size);
  }

  lock_release(&cache_lock);
  // lock_release(&buffer_cache[idx].lock);
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
  // struct inode_disk data; /* Inode content. */
  struct lock lock;
};

bool inode_resize(struct inode_disk* id, off_t size) {
  block_sector_t sector;
  /* Handle direct pointers. */
  for (int i = 0; i < 12; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && id->direct[i] != 0) {
      /* Shrink. */
      free_map_release(id->direct[i], 1);
      id->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && id->direct[i] == 0) {
      /* Grow. */
      bool check = free_map_allocate(1, &(id->direct[i]));
      if (!check) {
        inode_resize(id, id->length);
        return false;
      }
    }
  }

  /* Check if indirect pointers are needed. */
  if (id->indirect == 0 && size <= 12 * BLOCK_SECTOR_SIZE) {
    return true;
  }
  block_sector_t buffer[128];
  memset(buffer, 0, 512);
  if (id->indirect == 0) {
    /* Allocate indirect block. */
    bool check = free_map_allocate(1, &(id->indirect));
    if (!check) {
      inode_resize(id, id->length);
      return false;
    }
  } else {
    /* Read in indirect block. */
    cache_function(fs_device, id->indirect, buffer, false, BLOCK_SECTOR_SIZE, 0);
  }

  /* Handle indirect pointers. */
  for (int i = 0; i < 128; i++) {
    if (size <= (12 + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      /* Shrink. */
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else if (size > (12 + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      /* Grow. */
      bool check = free_map_allocate(1, &(buffer[i]));
      if (!check) {
        inode_resize(id, id->length);
        return false;
      }
    }
  }
  if (size <= 12 * BLOCK_SECTOR_SIZE) {
    /* We shrank the inode such that indirect pointers are not required. */
    free_map_release(id->indirect, 1);
    id->indirect = 0;
  } else {
    /* Write the updates to the indirect block back to disk. */
    cache_function(fs_device, id->indirect, buffer, true, BLOCK_SECTOR_SIZE, 0);
  }
  // DEBUG
  if (size == 75678) {
    printf("here\n");
  }
  /* Check if doubly indirect pointers are needed. */
  if (id->doubly_indirect == 0 && size <= 140 * BLOCK_SECTOR_SIZE) {
    return true;
  }
  block_sector_t buffer2[128];
  memset(buffer2, 0, 512);
  if (id->doubly_indirect == 0) {
    /* Allocate doubly indirect block. */
    bool check = free_map_allocate(1, &(id->doubly_indirect));
    if (!check) {
      inode_resize(id, id->length);
      return false;
    }
  } else {
    /* Read in doubly indirect block. */
    cache_function(fs_device, id->doubly_indirect, buffer2, false, BLOCK_SECTOR_SIZE, 0);
  }

  /* Handle doubly indirect pointers. */
  for (int i = 0; i < 128; i++) {
    /* Check if this indirect pointer is needed. */
    if (buffer2[i] == 0 && size <= (140 + 128 * i) * BLOCK_SECTOR_SIZE) {
      break;
    }
    memset(buffer, 0, 512);
    if (buffer2[i] == 0) {
      /* Allocate this indirect block. */
      bool check = free_map_allocate(1, &(buffer2[i]));
      if (!check) {
        inode_resize(id, id->length);
        return false;
      }
    } else {
      /* Read in this indirect block. */
      cache_function(fs_device, buffer2[i], buffer, false, BLOCK_SECTOR_SIZE, 0);
    }

    /* Handle this indirect pointer. */
    for (int j = 0; j < 128; j++) {
      if (size <= (140 + 128 * i + j) * BLOCK_SECTOR_SIZE && buffer[j] != 0) {
        /* Shrink. */
        free_map_release(buffer[j], 1);
        buffer[j] = 0;
      } else if (size > (140 + 128 * i + j) * BLOCK_SECTOR_SIZE && buffer[j] == 0) {
        /* Grow. */
        bool check = free_map_allocate(1, &(buffer[j]));
        if (!check) {
          inode_resize(id, id->length);
          return false;
        }
      }
    }
    
    if (size <= (140 + 128 * i) * BLOCK_SECTOR_SIZE) {
      /* We shrank the inode such that indirect pointers are not required. */
      free_map_release(buffer2[i], 1);
      buffer2[i] = 0;
    } else {
      /* Write the updates to the indirect block back to disk. */
      cache_function(fs_device, buffer2[i], buffer, true, BLOCK_SECTOR_SIZE, 0);
    }
  }
  // Handle update to doubly indir
  if (size <= 140 * BLOCK_SECTOR_SIZE) {
    /* We shrank the inode such that doubly indirect pointers are not required. */
    free_map_release(id->doubly_indirect, 1);
    id->doubly_indirect = 0;
  } else {
    /* Write the updates to the doubly indirect block back to disk. */
    cache_function(fs_device, id->doubly_indirect, buffer2, true, BLOCK_SECTOR_SIZE, 0);
  }

  return true;
}

void find_idx(struct inode_disk* id, int* direct_idx, int* indirect_idx, off_t offset) {
  int block_idx = offset/BLOCK_SECTOR_SIZE;
  int num_dir_ptrs = 12;
  int entries_in_ind_ptr = 128;
  if (block_idx < 12) {
    *direct_idx = block_idx;
    *indirect_idx = -2;
  } else if (block_idx < 140) {
    *direct_idx = block_idx - 12;
    *indirect_idx = -1;
  } else {
    *indirect_idx = (block_idx - num_dir_ptrs + entries_in_ind_ptr) / 128;
    *direct_idx = (block_idx - num_dir_ptrs + entries_in_ind_ptr) % 128;
  }
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  block_sector_t sector = inode->sector;
  struct inode_disk* id = calloc(1, sizeof(struct inode_disk));
  // block_read(fs_device, inode->sector, id);
  cache_function(fs_device, sector, id, false, BLOCK_SECTOR_SIZE, 0);
  int direct_idx;
  int indirect_idx;
  block_sector_t buffer[128];
  memset(buffer, 0, 512);
  find_idx(id, &direct_idx, &indirect_idx, pos);
  if (indirect_idx == -2) {
    sector = id->direct[direct_idx];
  } else if (indirect_idx == -1) {
    cache_function(fs_device, id->indirect, buffer, false, BLOCK_SECTOR_SIZE, 0);
    sector = buffer[direct_idx];
  } else {
    cache_function(fs_device, id->doubly_indirect, buffer, false, BLOCK_SECTOR_SIZE, 0);
    sector = buffer[indirect_idx];
    cache_function(fs_device, sector, buffer, false, BLOCK_SECTOR_SIZE, 0);
    sector = buffer[direct_idx];
  }
  free(id);
  return sector;
  
  // TODO: Delete later
  // if (pos < inode->data.length)
  //   return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  // else
  //   return -1;
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
    // size_t sectors = bytes_to_sectors(length);
    // disk_inode->length = length;
    // disk_inode->magic = INODE_MAGIC;
    // if (free_map_allocate(sectors, &disk_inode->start)) {
    //   cache_function(fs_device, sector, disk_inode, true, BLOCK_SECTOR_SIZE, 0);
    //   // block_write(fs_device, sector, disk_inode);
    //   if (sectors > 0) {
    //     static char zeros[BLOCK_SECTOR_SIZE];
    //     size_t i;

    //     for (i = 0; i < sectors; i++)
    //       cache_function(fs_device, disk_inode->start + i, zeros, true, BLOCK_SECTOR_SIZE, 0);
    //       // block_write(fs_device, disk_inode->start + i, zeros);
    //   }
    if (inode_resize(disk_inode, length)){
      disk_inode->length = length;
      cache_function(fs_device, sector, disk_inode, true, BLOCK_SECTOR_SIZE, 0);
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
  lock_acquire(&inode_list_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&inode_list_lock);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL) {
    lock_release(&inode_list_lock);
    return NULL;
  }
    

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  list_push_front(&open_inodes, &inode->elem);
  // block_read(fs_device, inode->sector, &inode->data);
  // cache_function(fs_device, inode->sector, &inode->data, false, BLOCK_SECTOR_SIZE, 0);
  lock_release(&inode_list_lock);
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
    lock_acquire(&inode_list_lock);
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      struct inode_disk* disk_inode = NULL;
      disk_inode = calloc(1, sizeof *disk_inode);
      lock_acquire(&inode->lock);
      cache_function(fs_device, inode->sector, disk_inode, false, BLOCK_SECTOR_SIZE, 0);
      inode_resize(disk_inode, 0);
      lock_release(&inode->lock);
      free(disk_inode);
      free_map_release(inode->sector, 1);
      // free_map_release(inode->data.start, bytes_to_sectors(inode->data.length));
    }

    free(inode);
    lock_release(&inode_list_lock);
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
    off_t length_inode = inode_length(inode);
    if (offset >= length_inode) 
      break;
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = length_inode - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
    //   /* Read full sector directly into caller's buffer. */
    //   // block_read(fs_device, sector_idx, buffer + bytes_read);
    //   cache_function(fs_device, sector_idx, buffer + bytes_read, false, BLOCK_SECTOR_SIZE, 0);
    // } else {
    //   /* Read sector into bounce buffer, then partially copy
    //          into caller's buffer. */
    //   if (bounce == NULL) {
    //     bounce = malloc(BLOCK_SECTOR_SIZE);
    //     if (bounce == NULL)
    //       break;
    //   }
    //   // block_read(fs_device, sector_idx, bounce);
    //   cache_function(fs_device, sector_idx, bounce, false, BLOCK_SECTOR_SIZE, 0);
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
  bool lock_acquired = false;
  struct inode_disk* disk_inode = NULL;

  if (inode->deny_write_cnt)
    return 0;
    
  off_t length_inode_old = inode_length(inode);
  if (offset + size >= length_inode_old) {
    disk_inode = calloc(1, sizeof *disk_inode);
    lock_acquire(&inode->lock);
    if (length_inode_old != inode_length(inode)) {
      lock_release(&inode->lock);
      return inode_write_at(inode, buffer_, size, offset);
    }

    lock_acquired = true;
    cache_function(fs_device, inode->sector, disk_inode, false, BLOCK_SECTOR_SIZE, 0);
    bool success = inode_resize(disk_inode, offset + size);
    if (!success) {
      lock_release(&inode->lock);
      return 0;
    }
  } 

  uint8_t zeroes[512];
  memset(zeroes, 0, 512);
  while (length_inode_old < offset) {
    /* Sector to write, starting byte length_inode_old within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, length_inode_old);
    int sector_ofs = length_inode_old % BLOCK_SECTOR_SIZE;

    /* Bytes left till offset, bytes left in sector, lesser of the two. */
    off_t inode_left = offset - length_inode_old;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = min_left;
    if (chunk_size <= 0)
      break;

    cache_function(fs_device, sector_idx, zeroes, true, chunk_size, sector_ofs);

    /* Advance. */
    length_inode_old += chunk_size;
  }
    
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
    //   // block_write(fs_device, sector_idx, buffer + bytes_written);
    //   cache_function(fs_device, sector_idx, buffer + bytes_written, true, BLOCK_SECTOR_SIZE, 0);
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
    //     // block_read(fs_device, sector_idx, bounce);
    //     cache_function(fs_device, sector_idx, bounce, false, BLOCK_SECTOR_SIZE, 0);
    //   else
    //     memset(bounce, 0, BLOCK_SECTOR_SIZE);
    //   memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
    //   // block_write(fs_device, sector_idx, bounce);
    //   cache_function(fs_device, sector_idx, bounce, true, BLOCK_SECTOR_SIZE, 0);
    // }
    cache_function(fs_device, sector_idx, buffer + bytes_written, true, chunk_size, sector_ofs);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  // free(bounce); // TODO: Don't forget to delete this with bounce
  

  if (lock_acquired) {
    disk_inode->length = offset + size;
    cache_function(fs_device, inode->sector, disk_inode, true, BLOCK_SECTOR_SIZE, 0);
    lock_release(&inode->lock);
    free(disk_inode);
  }
    
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
  // return inode->data.length; // TODO: Remove this line & implement inode_length properly
  struct inode_disk* disk_inode = NULL;
  disk_inode = calloc(1, sizeof *disk_inode);
  cache_function(fs_device, inode->sector, disk_inode, false, BLOCK_SECTOR_SIZE, 0);
  off_t len = disk_inode->length;
  free(disk_inode);
  return len; 
  }
