# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(hit-rate) begin
(hit-rate) end
hit-rate: exit(0)
EOF
pass;