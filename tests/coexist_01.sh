#!/bin/sh
#
# Gauge and small text together (independent bands)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge + small text coexist"
expect "Green caption reading: with-bar near the bottom"
expect "Green gauge tick at ~50% in the band below the caption"
expect "Caption sits above the bar; the two must not overlap"

show -g 50 -B "$( hold_arg )" -t with-bar "$( hold_arg )"
