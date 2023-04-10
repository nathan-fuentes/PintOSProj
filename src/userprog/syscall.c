#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "lib/float.h"


fd_map_t* find_fd_map(struct list* file_list, int fd);
struct lock* find_lock(struct list* lock_t_list, lock_t* l);
struct semaphore* find_sema(struct list* sema_t_list, sema_t* s);
bool validity_check(void* addr, int num_bytes);

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

/* Given a list of lock_t_maps and a lock_t, finds a lock_t_maps associated 
   with the given lock_t, returning NULL if none was found. */
struct lock* find_lock(struct list* lock_t_list, lock_t* l) {
  struct list_elem *e;

  for (e = list_begin(lock_t_list); e != list_end(lock_t_list); e = list_next(e)) {
    lock_t_map_t* lock_t_map = list_entry(e, lock_t_map_t, elem);
    if (l == lock_t_map->l) {
      return &(lock_t_map->lock);
    }
  }
  return NULL;
}

/* Given a list of sema_t_maps and a sema_t, finds a sema_t_maps associated 
   with the given sema_t, returning NULL if none was found. */
struct semaphore* find_sema(struct list* sema_t_list, sema_t* s) {
  struct list_elem *e;

  for (e = list_begin(sema_t_list); e != list_end(sema_t_list); e = list_next(e)) {
    sema_t_map_t* sema_t_map = list_entry(e, sema_t_map_t, elem);
    if (s == sema_t_map->s) {
      return &(sema_t_map->sema);
    }
  }
  return NULL;
}



static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Checks if a given address is null, invalid, or pointing to 
   kernel memory, returning true (valid) if none apply. */
bool validity_check(void* addr, int num_bytes) {
  for (int i = 0; i < num_bytes; i++) {
    char* address = ((char *) addr) + i;
    if (address == NULL) {
      return false;
    }
    if (!is_user_vaddr((void *) address)) {
      return false;
    }
    uint32_t* page_directory = thread_current()->pcb->pagedir;
    if (pagedir_get_page(page_directory, (void*) address) == NULL) {
      return false;
    } 
  }
  return true;
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  if (!validity_check((void *) args, 4)) {
    f->eax = -1;
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    process_exit(-1);
  }

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  switch (args[0]) {
    
    case SYS_PRACTICE:
      if (validity_check((void *) args, 8)) {
        *(int *)(&(args[1])) = args[1] + 1;
        f->eax = args[1];
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_HALT:
      if (validity_check((void *) args, 4)) {
        shutdown_power_off();
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_EXIT:
      if (validity_check((void *) args, 8)) {
        f->eax = args[1];
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
        // process_exit(args[1]);
        lock_acquire(&(thread_current()->pcb->lock));
        thread_current()->pcb->should_exit = args[1];
        if (is_main_thread(thread_current(), thread_current()->pcb)) {
          pthread_exit_main();
        } else {
          pthread_exit();
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_EXEC:
      if (validity_check((void *) args, 8) && validity_check((void *) args[1], 4)) {
        f->eax = process_execute(args[1]);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_WAIT:
      if (validity_check((void *) args, 8)) {
        f->eax = process_wait(args[1]);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_CREATE:
      if (validity_check((void *) args, 12) && validity_check((void *) args[1], 4)) {
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
      if (validity_check((void *) args, 8) && validity_check((void *) args[1], 4)) {
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
      if (validity_check((void *) args, 8) && validity_check((void *) args[1], 4)) {
        int fd = -1;
        lock_acquire(glob_lock);
        struct file* file = filesys_open(args[1]);
        lock_release(glob_lock);
        if (file != NULL) {
          fd_map_t* fd_map = (fd_map_t *) calloc(sizeof(fd_map_t), 1);
          fd = thread_current()->pcb->fd_tracker;
          fd_map->fd = fd;
          thread_current()->pcb->fd_tracker++;
          fd_map->file = file;
          list_push_back(thread_current()->pcb->fd_list, &(fd_map->elem));
        }
        f->eax = fd;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_FILESIZE:
      if (validity_check((void *) args, 8)) {
        int size;
        fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
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
      if (validity_check((void *) args, 16) && validity_check((void *) args[2], 4)) {
        if (args[1] != 0) {
          if (args[1] == 1) {
            f->eax = -1;
          } else {
            fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
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
          lock_acquire(glob_lock);
          char* result;
          uint8_t curr_key;
          int size = 1;
          while ((curr_key = input_getc()) != -1) {
            char curr_char = (char) curr_key;
            strlcpy(result, curr_char, 1);
            size++;
          }
          strlcpy(result, "\0", 1);
          args[2] = result;
          lock_release(glob_lock);
          f->eax = size;
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_WRITE:
      if (validity_check((void *) args, 16) && validity_check((void *) args[2], 4)) {
        if (args[1] == 1) {
          lock_acquire(glob_lock);
          putbuf(args[2], args[3]);
          f->eax = args[3];
          lock_release(glob_lock);
        } else {
          if (args[1] == 0) {
            f->eax = -1;
          } else {
            fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
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
      if (validity_check((void *) args, 12)) {
          fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
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
      if (validity_check((void *) args, 8)) {
        fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
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
      if (validity_check((void *) args, 8)) {
            fd_map_t* fd_map = find_fd_map(thread_current()->pcb->fd_list, args[1]);
            if (fd_map == NULL) {
              f->eax = -1;
            } else {
              struct file* file = fd_map->file;
              lock_acquire(glob_lock);
              file_close(file);
              list_remove(&(fd_map->elem));
              lock_release(glob_lock);
              free(fd_map);
            }
      } else {
          f->eax = -1;
          printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
          process_exit(-1);
      }
      break;

    case SYS_COMPUTE_E:
      if (validity_check((void *) args, 8)) {
        f->eax = sys_sum_to_e(args[1]);
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_PT_CREATE:
      if (validity_check((void *) args, 16)) {
        lock_acquire(&(thread_current()->pcb->lock));
        f->eax = pthread_execute(args[1], args[2], args[3]);
        lock_release(&(thread_current()->pcb->lock));
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
  
    case SYS_PT_EXIT:
      if (validity_check((void *) args, 4)) {
        if (is_main_thread(thread_current(), thread_current()->pcb)) {
          lock_acquire(&(thread_current()->pcb->lock));
          pthread_exit_main();
        } else {
          lock_acquire(&(thread_current()->pcb->lock));
          pthread_exit();
        }
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
  
    case SYS_PT_JOIN:
      if (validity_check((void *) args, 8)) {
        lock_acquire(&(thread_current()->pcb->lock));
        f->eax = pthread_join(args[1]);
        lock_release(&(thread_current()->pcb->lock));
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_LOCK_INIT:
      if (validity_check((void *) args, 8)) {
        // TODO: error check the calloc if needed
        if (args[1] == NULL) {
          f->eax = false;
          break;
        }
        lock_t_map_t *lock_t_map = calloc(1, sizeof(lock_t_map_t));
        lock_init(&(lock_t_map->lock));
        lock_t_map->l = (lock_t *) args[1];
        list_push_back(&(thread_current()->pcb->lock_t_list), &(lock_t_map->elem));
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_LOCK_ACQUIRE:
      if (validity_check((void *) args, 8)) {
        struct lock *l = find_lock(&(thread_current()->pcb->lock_t_list), (lock_t *) args[1]);
        if (l == NULL || l->holder == thread_current()) {
          f->eax = false;
          break;
        }
        lock_acquire(l);
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_LOCK_RELEASE:
      if (validity_check((void *) args, 8)) {
        struct lock *l = find_lock(&(thread_current()->pcb->lock_t_list), (lock_t *) args[1]);
        if (l == NULL || l->holder != thread_current()) {
          f->eax = false;
          break;
        }
        lock_release(l);
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;

    case SYS_SEMA_INIT:
      if (validity_check((void *) args, 12)) {
        if (args[1] == NULL || (int) args[2] < 0) {
          f->eax = false;
          break;
        }
        sema_t_map_t *sema_t_map = calloc(1, sizeof(sema_t_map_t));
        sema_init(&(sema_t_map->sema), (int) args[2]);
        sema_t_map->s = (sema_t *) args[1];
        list_push_back(&(thread_current()->pcb->sema_t_list), &(sema_t_map->elem));
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_SEMA_DOWN:
      if (validity_check((void *) args, 8)) {
        struct sema *s = find_sema(&(thread_current()->pcb->sema_t_list), (sema_t *) args[1]);
        if (s == NULL) {
          f->eax = false;
          break;
        }
        sema_down(s);
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_SEMA_UP:
      if (validity_check((void *) args, 8)) {
        struct sema *s = find_sema(&(thread_current()->pcb->sema_t_list), (sema_t *) args[1]);
        if (s == NULL) {
          f->eax = false;
          break;
        }
        sema_up(s);
        f->eax = true;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
    
    case SYS_GET_TID:
      if (validity_check((void *) args, 4)) {
        f->eax = thread_current()->tid;
      } else {
        f->eax = -1;
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
        process_exit(-1);
      }
      break;
  }
} 