# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF', <<'EOF']);
(close-null) begin
(close-null) end
close-null: exit(0)
EOF
(close-null) begin
close-null: exit(-1)
EOF
pass;
