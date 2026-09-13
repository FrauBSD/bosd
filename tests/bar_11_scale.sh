#!/bin/sh
#
# Gauge size multiplier (-s)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge half scale"
expect "Green tick bar at ~55% fill"
expect "Ticks about half the default height and width"
expect "Bar still seated in the same bottom band (not floating up)"
expect "Black outline present, scaled with the ticks"

show -s 0.5 -g 55 -B "$( hold_arg )"

test_begin "gauge 125% scale with labels"
expect "Green tick bar at ~60% fill, about 1.25x default size"
expect "Prefix Hi and append Lo match the taller tick-derived face"
expect "Still bottom-seated; labels centerline-aligned with the bar"

show -s 1.25 -g 60 -p Hi -a Lo -B "$( hold_arg )"
