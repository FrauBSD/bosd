#!/bin/sh
#
# Small caption offsets (-x / -y)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text offsets"
expect "Caption reading: xy-cap near the bottom band"
expect "Shifted right ~200px and up ~80px from the default seat"
expect "Compare against stext_01's default placement"

show -x 200 -y -80 -t xy-cap "$( hold_arg )"
