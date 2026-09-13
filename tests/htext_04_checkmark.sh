#!/bin/sh
#
# Large text Unicode checkmark (\u2713)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text checkmark"
expect "Huge centered checkmark glyph (U+2713)"
expect "White fill with a thick black outline"
expect "Embedded \\u2713 in the -T operand decodes to the mark"

show -T '\u2713' "$( hold_arg )"
