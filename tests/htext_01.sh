#!/bin/sh
#
# Large outlined text (-T)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text"
expect "Huge centered letters reading: OK"
expect "White-ish fill with a thick black outline"
expect "Parked on the primary panel, not the bottom caption band"

show -T OK "$( hold_arg )"
