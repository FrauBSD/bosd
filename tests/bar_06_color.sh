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

test_begin "gauge label color distinct from ticks"
expect "Green tick bar at ~70% fill (default -G)"
expect "Prefix Hi and append Lo filled white (-F), not green"
expect "Black outline still present on ticks and labels"

show -F white -g 70 -p Hi -a Lo -B "$( hold_arg )"

test_begin "gauge overage label color with -F"
expect "Full red bar (-G) plus overage 130% in yellow (-F)"
expect "Append x also yellow; ticks stay red"

show -G red -F yellow -g 130 -a x -B "$( hold_arg )"
