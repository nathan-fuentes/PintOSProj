/* This program attempts to tell memory at an address that is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int handle = tell(162);
  if (handle != -1){
    fail("tell() returned %d", handle);
  }
}
