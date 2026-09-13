#!/bin/sh
#
# Small caption text (-t), opaque default path
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text opaque"
expect "One green caption line near the panel bottom"
expect "Text reads: visual-stext"
expect "Black outline around the glyphs"
expect "Sits above the gauge band (even with no bar showing)"

show -t visual-stext "$( hold_arg )"
