#!/bin/sh
#
# Example PNG glyph, no badge
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon plain"
expect "FreeBSD-red BSD wordmark glyph, centered on the panel"
expect "Soft baked translucency (~80% in the PNG) without -A"
expect "Black outline halo around the letterforms"
expect "No superscript badge"

show "$png" "$( hold_arg )"
