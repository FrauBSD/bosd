#!/bin/sh
#
# Icon with badge, captions above and below, plus gauge
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon + badge + captions + gauge"
expect "BSD wordmark with white badge '5' at the upper-right of the ink"
expect "Caption above reading: over-icon"
expect "Caption below reading: under-icon"
expect "Green gauge tick at ~40% in the bottom band"
expect "Captions hug the glyph; bar stays in its own band"

show -g 40 -B "$( hold_arg )" -b 5 -p over-icon -a under-icon \
    "$png" "$( hold_arg )"
