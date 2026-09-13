#!/bin/sh
#
# Large text scale (-s)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text scale"
expect "Centered letters reading: OK at half the default size (-s 0.5)"
expect "Still outlined; compare against htext_01's full panel height"

show -s 0.5 -T OK "$( hold_arg )"
