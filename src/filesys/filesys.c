#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  lock_acquire(&cache_lock);
  for (int i = 0; i < 64; i++) {
    // cache_entry_t cur = buffer_cache[i];
    buffer_cache[i].valid_bit = false;
    if (buffer_cache[i].dirty_bit) {
      lock_acquire(&buffer_cache[i].lock);
      block_write(fs_device, buffer_cache[i].sector_number, &buffer_cache[i].data);
      lock_release(&buffer_cache[i].lock);
    }
  }
  lock_release(&cache_lock); 
  free_map_close();
}

// /* Creates a file named NAME with the given INITIAL_SIZE.
//    Returns true if successful, false otherwise.
//    Fails if a file named NAME already exists,
//    or if internal memory allocation fails. */
// bool filesys_create(const char* name, off_t initial_size) {
//   block_sector_t inode_sector = 0;
//   struct dir* dir = dir_open_root();
//   bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
//                   inode_create(inode_sector, initial_size, false) && dir_add(dir, name, inode_sector));
//   if (!success && inode_sector != 0)
//     free_map_release(inode_sector, 1);
//   dir_close(dir);

//   return success;
// }

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size) {
  struct dir* dir;
  // struct dir* dir = dir_open_root();
  if (name[0] == '/'){
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->pcb->cwd);
  }
  
  struct inode* inode = NULL;
  char part[NAME_MAX+1];
  memset(part, 0, NAME_MAX+1);
  int next = get_next_part(part, &name);

  while(next == 1) {
    if (dir == NULL) 
    inode = NULL;
    if (dir != NULL)
      dir_lookup(dir, part, &inode);
    if (inode == NULL) {
      break;
    }
    if (inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    }
    memset(part, 0, NAME_MAX+1);
    next = get_next_part(part, &name);
  }
  if (next == -1) {
    inode = NULL;
  }
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size, false) && dir_add(dir, part, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

struct inode* filesys_open_inode(const char* name) {
  struct dir* dir;
  // struct dir* dir = dir_open_root();
  if (name[0] == '/'){
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->pcb->cwd);
  }
  
  struct inode* inode = NULL;
  char part[NAME_MAX+1];
  memset(part, 0, NAME_MAX+1);
  int next = get_next_part(part, &name);

  while(next == 1) {
    if (dir == NULL) 
    inode = NULL;
    if (dir != NULL)
      dir_lookup(dir, part, &inode);
    if (inode == NULL) {
      break;
    }
    if (inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    }
    memset(part, 0, NAME_MAX+1);
    next = get_next_part(part, &name);
  }
  if (next == -1) {
    inode = NULL;
  }
  dir_close(dir);

  return inode;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
  struct dir* dir = dir_open_root();
  struct inode* inode = NULL;

  if (dir != NULL)
    dir_lookup(dir, name, &inode);
  dir_close(dir);

  return file_open(inode);
}

// /* Deletes the file named NAME.
//    Returns true if successful, false on failure.
//    Fails if no file named NAME exists,
//    or if an internal memory allocation fails. */
// bool filesys_remove(const char* name) {
//   struct dir* dir = dir_open_root();
//   bool success = dir != NULL && dir_remove(dir, name);
//   dir_close(dir);

//   return success;
// }

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) { // TODO: THIS NEEDS MAJOR FIXING
  struct dir* dir;
  struct dir* old_dir;
  // struct dir* dir = dir_open_root();
  if (name[0] == '/'){
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->pcb->cwd);
  }
  old_dir = dir_reopen(dir);
  
  struct inode* inode = NULL;
  char part[NAME_MAX+1];
  char last_part[NAME_MAX+1];
  memset(part, 0, NAME_MAX+1);
  memset(last_part, 0, NAME_MAX+1);
  int next = get_next_part(part, &name);

  while(next == 1) {
    inode = NULL;
    if (dir != NULL)
      dir_lookup(dir, part, &inode);
    if (inode == NULL) {
      break;
    }
    if (inode_is_dir(inode)) {
      dir_close(old_dir);
      old_dir = dir_reopen(dir);
      dir_close(dir);
      dir = dir_open(inode);
    }
    memcpy(last_part, part, NAME_MAX+1);
    next = get_next_part(part, &name);
  }
  if (next == -1) {
    inode = NULL;
  }
  if (inode == NULL || open_cnt(inode) > 0) {
    if (dir != NULL) dir_close(dir);
    if (old_dir != NULL) dir_close(old_dir);
    return false;
  }

  if (open_cnt(inode) > 0) {
    if (dir != NULL) dir_close(dir);
    if (old_dir != NULL) dir_close(old_dir);
    return false;
  }
  bool success;
  if (inode_is_dir(inode)) {
    success = dir != NULL && dir_remove(old_dir, last_part);
  } else {
    success = dir != NULL && dir_remove(dir, last_part);
  }
  // bool success = dir != NULL && dir_remove(dir, last_part);

  dir_close(dir);
  dir_close(old_dir);
  return success;

}

bool filesys_chdir(const char* name) {
  struct dir* dir;
  // struct dir* dir = dir_open_root();
  if (name[0] == '/'){
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->pcb->cwd);
  }
  
  struct inode* inode = NULL;
  char part[NAME_MAX+1];
  memset(part, 0, NAME_MAX+1);
  int next = get_next_part(part, &name);

  while(next == 1) {
    inode = NULL;
    if (dir != NULL)
      dir_lookup(dir, part, &inode);
    if (inode == NULL) {
      break;
    }
    if (inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    }
   next = get_next_part(part, &name);
  }
  if (next == -1) {
    inode = NULL;
  }
  if (dir == NULL) {
    return false;
  }
  if (inode == NULL) {
    dir_close(dir);
    return false;
  }
  
  if (!inode_is_dir(inode)) {
    dir_close(dir);
    return false;
  }
  
  dir_close(thread_current()->pcb->cwd);
  thread_current()->pcb->cwd = dir;
  return true;
}

bool filesys_mkdir(const char* name) {
  struct dir* dir;
  // struct dir* dir = dir_open_root();
  if (name[0] == '/'){
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->pcb->cwd);
  }
  
  struct inode* inode = dir_get_inode(dir);
  struct inode* last_inode = NULL;
  char part[NAME_MAX+1];
  char last_part[NAME_MAX+1];
  memset(part, 0, NAME_MAX+1);
  memset(last_part, 0, NAME_MAX+1);
  int next = get_next_part(part, &name);

  while(next == 1) {
    last_inode = inode;
    inode = NULL;
    if (dir != NULL)
      dir_lookup(dir, part, &inode);
    if (inode == NULL) {
      break;
    }
    if (inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    }
    memcpy(last_part, part, NAME_MAX+1);
    next = get_next_part(part, &name);
  }
  if (next == -1) {
    last_inode = NULL;
  }
  if (last_inode == NULL) {
    dir_close(dir);
    return false;
  }
  block_sector_t block_sector;
  if (!free_map_allocate(1, &block_sector)) {
    return false;
  }
  if (!dir_create(block_sector, 2)) {
    free_map_release(1, block_sector);
    return false;
  }
 
  struct dir* new_dir = dir_open(inode_open(block_sector));
  if (new_dir == NULL || !dir_add(dir, part, block_sector)){
    if (!dir_remove(dir, part)){
      free_map_release(1, block_sector);
    }
    return false;
  }
  dir_add(new_dir, ".", block_sector);
  dir_add(new_dir, "..", inode_get_inumber(last_inode));
  dir_close(new_dir);
  dir_close(dir);
  return true;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  struct dir* root_dir = dir_open_root();
  dir_add(root_dir, ".", ROOT_DIR_SECTOR);
  dir_add(root_dir, "..", ROOT_DIR_SECTOR);
  thread_current()->pcb->cwd = root_dir;
  free_map_close();
  printf("done.\n");
}
