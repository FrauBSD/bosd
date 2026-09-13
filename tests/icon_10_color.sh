#!/bin/sh
#
# Badge fill color (-F); requires -b on an icon
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon badge color"
expect "BSD wordmark with badge '8' at the upper-right of the ink"
expect "Badge fill is orange (not the default white)"

show -b 8 -F orange "$png" "$( hold_arg )"
