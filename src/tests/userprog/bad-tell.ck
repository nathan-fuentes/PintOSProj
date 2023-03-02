# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(bad-tell) begin
(bad-tell) end
bad-tell: exit(0)
EOF
pass;
