#!/bin/sh
#
# Large text fill/outline translucency (-T -A -O)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text translucent"
expect "Huge centered letters reading: FADE"
expect "Fill opacity 0.45; outline opacity 0.8"
note "With a compositor, desktop shows through the fill"

show -A 0.45 -O 0.8 -T FADE "$( hold_arg )"
