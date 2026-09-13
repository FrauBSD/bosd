#!/bin/sh
#
# Gauge fill color (-G)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge custom color"
expect "Tick bar at ~60% fill"
expect "Ticks are red (not the default green #2AC12A)"
expect "Black outline still present on the ticks"

show -G red -g 60 -B "$( hold_arg )"
