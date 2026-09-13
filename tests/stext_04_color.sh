#!/bin/sh
#
# Small caption fill color (-F)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text color"
expect "Caption near the panel bottom reading: color-cap"
expect "Fill is red (not the default green)"
expect "Black outline still present"

show -F red -t color-cap "$( hold_arg )"
