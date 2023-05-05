/* Tests that the buffer cache can coalesce  */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[64000];

void test_main(void) {
    char* file_name = "write_count";
    int fd;
    random_bytes(buf, 64000);
    CHECK(create(file_name, 64000), "create \"%s\"", file_name);
    CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);

    write(fd, buf, 64000);
    msg("write count %d", write_count());
    msg("close \"%s\"", file_name);
    close(fd);
}