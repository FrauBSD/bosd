#!/bin/sh
#
# Large text with superscript badge (-b)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text badge"
expect "Huge centered letters reading: OK"
expect "Small white badge '1' as a superscript past the text's right edge"
expect "Badge sits above the main letters, black-outlined"

show -b 1 -T OK "$( hold_arg )"
