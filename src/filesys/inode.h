#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

struct bitmap;

typedef struct cache_entry {
  block_sector_t sector_number;     /* Used for ID'ing the block */
  bool dirty_bit;                   /* Indicates whether the block has been altered or not */
  bool clock_bit;                   /* Used for clock algorithm */
  bool valid_bit;                   /* Indicates whether this entry is in use */
  struct lock lock;                 /* Cache Entry-Level Lock */
  uint8_t data[BLOCK_SECTOR_SIZE];  /* Data of the block */
} cache_entry_t;

struct lock cache_lock; /* Cache-Level Lock */
struct lock inode_list_lock; /* Inode List Lock */
cache_entry_t buffer_cache[64]; /* Array representing Buffer Cache (may need to be on the heap, but should be good as a global variable) */
void cache_function(struct block* block, block_sector_t sector_number, void* buffer, bool write, off_t size, off_t offset);

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(const struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
off_t inode_length(const struct inode*);

int open_cnt(struct inode* node);
bool inode_is_dir(struct inode* node);
double get_cache_hit_rate(void);
int fs_device_write_count(void);

#endif /* filesys/inode.h */
