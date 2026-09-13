#!/bin/sh
#
# Large text and badge share fill color (-F with -b)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text badge color"
expect "Huge centered letters reading: OK in orange"
expect "Badge '1' at the upper-right also orange (same -F)"
expect "Both outlined in black; compare against htext_05 (white badge)"

show -b 1 -F orange -T OK "$( hold_arg )"
