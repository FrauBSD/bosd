#!/bin/sh
#
# Small caption translucency (-t -A -O)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text translucent"
expect "Caption near the panel bottom reading: fade-caption"
expect "Fill washed out (opacity 0.4); outline softer (0.75)"
expect "ARGB path (not the classic shaped XLFD window)"
note "With a compositor, desktop shows through the letters"

show -A 0.4 -O 0.75 -t fade-caption "$( hold_arg )"
