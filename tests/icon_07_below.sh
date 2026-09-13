#!/bin/sh
#
# Caption below the glyph (-a)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon caption below"
expect "BSD wordmark centered on the panel"
expect "White outlined caption under the glyph reading: below-glyph"

show -a below-glyph "$png" "$( hold_arg )"
