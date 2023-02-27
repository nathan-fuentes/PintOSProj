#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* Given a list of fd_maps and a fd, finds a file associated 
   with the given fd, returning NULL if none was found. */
struct file* find_file(struct list* file_list, int fd) {
  struct list_elem *e;

  for (e = list_begin(file_list); e != list_end(file_list); e = list_next(e)) {
    fd_map_t* fd_map = list_entry(e, fd_map_t, elem);
    if (fd == fd_map->fd) {
      return fd_map->file;
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
    if (lookup_page(page_directory, address, false) == NULL) {
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
      break;

    case SYS_REMOVE:
      break;

    case SYS_OPEN:
      break;

    case SYS_FILESIZE:
      break;

    case SYS_READ:
      break;

    case SYS_WRITE:
      if (validity_check(&args[0]) && validity_check(&args[1]) && validity_check(&args[2]) && validity_check(args[2]) && validity_check(&args[3])) {
        if (args[1] == 1) {
          // TODO: Account for large buffer sizes
          lock_acquire(glob_lock);
          putbuf(args[2], args[3]);
          lock_release(glob_lock);
        } // TODO: Else case for not stdout
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_SEEK:
      break;

    case SYS_TELL:
      break;

    case SYS_CLOSE:
      break;
  }
}
