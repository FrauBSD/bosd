#!/bin/sh
#
# Glyph scale with badge (-s and -b); badge must grow with the glyph
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon scale with badge"
expect "BSD wordmark at twice the default panel-derived size (-s 2)"
expect "White badge '8' at the upper-right, scaled up with the glyph"
expect "Compare badge size against icon_02_badge at scale 1"

show -s 2 -b 8 "$png" "$( hold_arg )"
