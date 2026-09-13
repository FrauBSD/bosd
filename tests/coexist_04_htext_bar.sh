#!/bin/sh
#
# Large text with gauge bar
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text + gauge"
expect "Huge centered letters reading: OK"
expect "Green gauge tick at ~60% in the bottom band"
expect "Text and bar up together; bar does not cover the letters"

show -g 60 -B "$( hold_arg )" -T OK "$( hold_arg )"
