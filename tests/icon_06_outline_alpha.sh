#!/bin/sh
#
# Glyph outline opacity (-O)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon outline opacity"
expect "BSD wordmark with native fill (no -A)"
expect "Black outline halo washed out (opacity 0.35)"
note "With a compositor, the halo is softer against the desktop"

show -O 0.35 "$png" "$( hold_arg )"
