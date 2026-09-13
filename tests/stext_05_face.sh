#!/bin/sh
#
# Small caption typeface (-f)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text face"
expect "Caption near the panel bottom reading: face-cap"
expect "Glyphs use Courier (or the nearest Courier face)"
expect "Compare letterforms against the default misc-fixed/Xft cases"

show -f Courier -t face-cap "$( hold_arg )"
