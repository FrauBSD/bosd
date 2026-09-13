#!/bin/sh
#
# Fast half-scale countdown with badge and captions (-s scales all)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown scale with badge and captions"
expect "Digits 25 down to 1 at half size (-s 0.5), ~0.1s each"
expect "White badge 'go' at the upper-right, scaled with the digits"
expect "Caption above reading: over-half (scaled with -s, not full size)"
expect "Caption below reading: under-half (scaled with -s, not full size)"
expect "Compare adornment size against coexist_06 at scale 1"
note "Fixed 0.1s hold (~2.5s total); -H does not apply here"
note "Digits tick in the background; ENTER kills and advances"

show_tick -s 0.5 -b go -p over-half -a under-half -c 25 0.1
