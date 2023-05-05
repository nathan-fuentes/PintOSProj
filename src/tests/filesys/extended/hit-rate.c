/* Tests that the buffer cache increases the hit rate when working 
    with the same file 2 calls in a row. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static int zero_buf[32768];

void test_main(void) {
    int zero_fd = open("zeros.txt");
    int first_rate;
    int second_rate;
    
    read(zero_fd, zero_buf, 32768);
    cache_hit_rate(); // Call this here to reset the cache counts from the previous filling of the buffer.
    char buf[256];

    int fd = open("buff_test.txt");

    for (int i = 0; i < 10; i++) {
        read(fd, buf, 1);
    }
    int x = cache_hit_rate();
    close(fd);
    int fd2 = open("buff_test.txt");

    for (int i = 0; i < 10; i++) {
        read(fd2, buf, 1);
    }
    int y = cache_hit_rate();

    if (x > y) {
        fail("First Rate is Higher Than Second");
    }
}