#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/* Checks if a given address is null, invalid, or pointing to 
   kernel memory, returning true (valid) if none apply */
bool validity_check(void* address) {
  if (address == NULL) {
    return false;
  }
  uint32_t* page_directory = thread_current()->pcb->pagedir;
  if (lookup_page(page_directory, address, false) == NULL) {
    return false;
  } 
  if (address > PHYS_BASE - 4) {
    return false;
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

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  } else if (args[0] == SYS_PRACTICE) {
    if (validity_check(&args[1])) {
      *(int *)(&(args[1])) = args[1] + 1;
      f->eax = args[1];
    } else {
      printf("Error: Invalid address given.");
      process_exit();
    }
  }
}
