#!/bin/sh
#
# Glyph with superscript badge (-b)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon badge"
expect "BSD wordmark glyph centered on the panel"
expect "Small white badge '8' at the upper-right of the ink"

show -b 8 "$png" "$( hold_arg )"
