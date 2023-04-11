/* Checks that we error when we try to release a lock that the thread never acquired. */

#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

void test_main(void) {
  lock_t lock;
  lock_check_init(&lock);
  lock_release(&lock);
  fail("Releasing an unowned lock succeeded");
}