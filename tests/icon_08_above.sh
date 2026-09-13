#!/bin/sh
#
# Caption above the glyph (-p)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon caption above"
expect "BSD wordmark centered on the panel"
expect "White outlined caption over the glyph reading: above-glyph"

show -p above-glyph "$png" "$( hold_arg )"
