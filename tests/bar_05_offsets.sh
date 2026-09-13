#!/bin/sh
#
# Gauge nudged with negative -x / -y
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge negative offsets"
expect "Green tick bar at ~50% fill"
expect "Whole bar shifted left ~200px and up ~200px from the default seat"
expect "Still in the bottom band region, just translated"

show -x -200 -y -200 -g 50 -B "$( hold_arg )"
