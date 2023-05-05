# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(write-cnt) begin
(write-cnt) create "write_count"
(write-cnt) open "write_count"
(write-cnt) write count 383
(write-cnt) close "write_count"
(write-cnt) end
EOF
pass;
