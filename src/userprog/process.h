#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;
typedef char lock_t;
typedef char sema_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

typedef struct shared_data {
  struct semaphore sema;         /* Used for scheduled waiting */
  struct lock lock;             /* Used for critical sections (ex: ref_cnt) */
  int ref_cnt;                   /* Used to keep track of num threads referencing this struct */
  int status;                    /* Used to keep track of exit status */
  pid_t pid;                     /* Helps parent identify specific child */
  struct list_elem elem;         /* Necessary for list implementationte */
} shared_data_t;

typedef struct fd_map {
  int fd;                   /* "Key" file descriptor */
  struct file* file;        /* "Value" file pointer */
  struct list_elem elem;    /* Necessary for list implementation */
} fd_map_t;

typedef struct lock_t_map {
  lock_t* l;                 /* "Key" user lock_t */
  struct lock lock;         /* "Value" associated kernel lock */
  struct list_elem elem;    /* Necessary for list implementation */
} lock_t_map_t;

typedef struct sema_t_map {
  sema_t* s;                 /* "Key" user sema_t */
  struct semaphore sema;    /* "Value" associated kernel semaphore */
  struct list_elem elem;    /* Necessary for list implementation */
} sema_t_map_t;

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  shared_data_t* shared_data; /* Connects this process to its parent (if it has one) */
  struct list child_list;     /* List of shared_data* with child processes */
  struct lock lock;          /* Used for critical sections (ex: process's pagedir), not needed until Project 2 */
  struct list* fd_list;       /* Ptr to file descriptor list struct */
  int fd_tracker;             /* Global file descriptor "counter" */
  struct file* file;          /* File ptr */
  struct list thread_list;    /* List of kernel threads */
  struct list lock_t_list;    /* Mapping of lock_t to kernel lock */
  struct list sema_t_list;    /* Mapping of sema_t to kernel lock */
  bool is_parent;
  int num_stack_pages;        /* How many pages are currently taken up under PHYS_BASE */
  int should_exit;           /* Indicator on if we should jump back to  */
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(int status);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
