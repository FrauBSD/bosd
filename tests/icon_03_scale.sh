#!/bin/sh
#
# Glyph scale (-s)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon scale"
expect "BSD wordmark at twice the default panel-derived size (-s 2)"
expect "Still centered; may approach or clip panel edges"

show -s 2 "$png" "$( hold_arg )"
