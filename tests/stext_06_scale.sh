#!/bin/sh
#
# Small caption scale (-s)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text scale"
expect "Caption near the panel bottom reading: scale-cap"
expect "Twice the default 24px caption size (-s 2)"
expect "Compare height against stext_01"

show -s 2 -t scale-cap "$( hold_arg )"
