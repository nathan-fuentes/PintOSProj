#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* Given a list of fd_maps and a fd, finds a fd_map associated 
   with the given fd, returning NULL if none was found. */
fd_map_t* find_fd_map(struct list* file_list, int fd) {
  struct list_elem *e;

  for (e = list_begin(file_list); e != list_end(file_list); e = list_next(e)) {
    fd_map_t* fd_map = list_entry(e, fd_map_t, elem);
    if (fd == fd_map->fd) {
      return fd_map;
    }
  }
  return NULL;
}

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Checks if a given address is null, invalid, or pointing to 
   kernel memory, returning true (valid) if none apply. */
bool validity_check(void* addr) {
  for (int i = 0; i < 4; i++) {
    char* address = ((char *) addr) + i;
    if (address == NULL) {
      return false;
    }
    if (is_kernel_vaddr(address)) {
      return false;
    }
    uint32_t* page_directory = thread_current()->pcb->pagedir;
    if (pagedir_get_page(page_directory, address) == NULL) {
      return false;
    } 
  }
  return true;
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  switch (args[0]) {
    
    case SYS_PRACTICE:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
        *(int *)(&(args[1])) = args[1] + 1;
        f->eax = args[1];
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_HALT:
      shutdown_power_off();
      break;

    case SYS_EXIT:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
        struct list_elem *e;
        struct list* file_list = &(thread_current()->pcb->fd_list);

        for (e = list_begin(file_list); e != list_end(file_list); e = list_next(e)) {
          fd_map_t* fd_map = list_entry(e, fd_map_t, elem);
          lock_acquire(glob_lock);
          file_close(fd_map->file);
          lock_release(glob_lock);
          list_remove(&(fd_map->elem));
          free(fd_map);
        }
        // free(file_list); // TODO: If implement this, make sure to calloc file_list AND child_list

        f->eax = args[1];
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
        process_exit(args[1]);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_EXEC:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(args[1])) {
        lock_acquire(glob_lock);
        f->eax = process_execute(args[1]);
        lock_release(glob_lock);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_WAIT:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
        f->eax = process_wait(args[1]);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_CREATE:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(args[1]) && validity_check(&args[2])) {
        lock_acquire(glob_lock);
        f->eax = filesys_create(args[1], args[2]);
        lock_release(glob_lock);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_REMOVE:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(args[1])) {
        lock_acquire(glob_lock);
        f->eax = filesys_remove(args[1]);
        lock_release(glob_lock);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_OPEN:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(args[1])) {
        int fd = -1;
        lock_acquire(glob_lock);
        struct file* file = filesys_open(args[1]);
        if (file != NULL) {
          fd_map_t* fd_map = (fd_map_t *) calloc(sizeof(fd_map_t), 1);
          fd = thread_current()->pcb->fd_tracker;
          fd_map->fd = fd;
          thread_current()->pcb->fd_tracker++;
          fd_map->file = file;
        }
        lock_release(glob_lock);
        f->eax = fd;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_FILESIZE:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
        int size;
        fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
        if (fd_map == NULL) {
          f->eax = -1;
        } else {
          struct file* file = fd_map->file;
          lock_acquire(glob_lock);
          size = file_length(file);
          lock_release(glob_lock);
          f->eax = size;
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_READ:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(&args[2]) && validity_check(args[2]) && validity_check(&args[3])) {
        if (args[1] != 0) {
          if (args[1] == 1) {
            f->eax = -1;
          } else {
            fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
            if (fd_map == NULL) {
              f->eax = -1;
            } else {
              struct file* file = fd_map->file;
              lock_acquire(glob_lock);
              f->eax = file_read(file, args[2], args[3]);
              lock_release(glob_lock);
            }
          }
        } else {
          // TODO: STDIN CASE
          lock_acquire(glob_lock);
          char* result;
          uint8_t curr_key;
          while ((curr_key = input_getc()) != -1) { // Change to EOF if possible
            char curr_char = (char) curr_key;
            strlcpy(result, curr_char, 1);
          }
          strlcpy(result, "\0", 1);
          args[2] = result;
          lock_release(glob_lock);
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_WRITE:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(&args[2]) && validity_check(args[2]) && validity_check(&args[3])) {
        if (args[1] == 1) {
          lock_acquire(glob_lock);
          // TODO: Fix Below to account for large buffer sizes
          /*for (int i = 0; i < args[3]; i += 256) {
            putbuf(args[2] + i, 256);
          }*/
          putbuf(args[2], args[3]);
          f->eax = args[3];
          lock_release(glob_lock);
        } else {
          if (args[1] == 0) {
            f->eax = -1;
          } else {
            fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
            if (fd_map == NULL) {
              f->eax = -1;
            } else {
              struct file* file = fd_map->file;
              lock_acquire(glob_lock);
              f->eax = file_write(file, args[2], args[3]);
              lock_release(glob_lock);
            }
          }
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_SEEK:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(&args[2])) {
          fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
          if (fd_map == NULL) {
            f->eax = -1;
          } else {
            struct file* file = fd_map->file;
            lock_acquire(glob_lock);
            file_seek(file, args[2]);
            lock_release(glob_lock);
          }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_TELL:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
        fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
        if (fd_map == NULL) {
          f->eax = -1;
        } else {
          struct file* file = fd_map->file;
          lock_acquire(glob_lock);
          f->eax = file_tell(file);
          lock_release(glob_lock);
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_CLOSE:
      if (validity_check(&args[0]) && validity_check(&args[1])) {
            fd_map_t* fd_map = find_fd_map(&(thread_current()->pcb->fd_list), args[1]);
            if (fd_map == NULL) {
              f->eax = -1;
            } else {
              struct file* file = fd_map->file;
              lock_acquire(glob_lock);
              file_close(file);
              list_remove(&(fd_map->elem));
              lock_release(glob_lock);
            }
      } else {
          f->eax = -1;
          printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
          process_exit(-1);
      }
      break;
  }
}
