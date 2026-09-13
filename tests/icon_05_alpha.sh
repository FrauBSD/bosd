#!/bin/sh
#
# Glyph fill opacity (-A)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon fill opacity"
expect "BSD wordmark with fill peaking at opacity 0.35"
expect "Outline halo still from native coverage (default -O)"
note "With a compositor, desktop shows through the fill"

show -A 0.35 "$png" "$( hold_arg )"
