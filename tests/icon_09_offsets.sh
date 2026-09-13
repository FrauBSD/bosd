#!/bin/sh
#
# Glyph offsets (-x / -y)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon offsets"
expect "BSD wordmark shifted right ~120px and down ~80px from center"
expect "Compare against icon_01's centered seat"

show -x 120 -y 80 "$png" "$( hold_arg )"
