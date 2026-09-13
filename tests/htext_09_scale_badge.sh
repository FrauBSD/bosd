#!/bin/sh
#
# Large text scale with badge (-s with -b); badge must shrink with -T
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text scale with badge"
expect "Centered letters reading: OK at half the default size (-s 0.5)"
expect "White badge '1' at the upper-right, scaled down with the text"
expect "Compare badge size against htext_05_badge at scale 1"

show -s 0.5 -b 1 -T OK "$( hold_arg )"
