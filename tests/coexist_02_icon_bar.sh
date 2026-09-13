#!/bin/sh
#
# Icon glyph with gauge bar
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon + gauge"
expect "BSD wordmark centered on the panel"
expect "Green gauge tick at ~45% in the bottom band"
expect "Glyph and bar up together; bar is below the artwork"

show -g 45 -B "$( hold_arg )" "$png" "$( hold_arg )"
