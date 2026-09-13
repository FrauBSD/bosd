#!/bin/sh
#
# Panel offsets (-x / -y) on large text
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text with offsets"
expect "Huge letters reading: XY"
expect "Shifted right ~400px and down ~300px from panel center"
expect "Still outlined; the nudge should be obvious against htext_01"

show -x 400 -y 300 -T XY "$( hold_arg )"
