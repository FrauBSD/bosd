#!/bin/sh
#
# Small caption with outline suppressed (-t -o)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text without outline"
expect "Green caption reading: no-halo"
expect "No black outline; fill glyphs only"

show -o -t no-halo "$( hold_arg )"
