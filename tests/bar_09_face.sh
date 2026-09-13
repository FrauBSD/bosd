#!/bin/sh
#
# Gauge-alone label face (-f with -p / -a / overage)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge captions with custom face"
expect "Green tick bar at ~55% fill"
expect "Prefix Pre and append Post in Courier (not Fixed)"
expect "Same tick-derived size as the default overage label"
expect "Green fill and black outline; centerlines on the bar"

show -f Courier -g 55 -p Pre -a Post -B "$( hold_arg )"

test_begin "gauge overage with custom face"
expect "Full bar plus overage text 160% in Courier"
expect "Append unit left-justified just past the overage text"
expect "Overage and append share the Courier face and size"

show -f Courier -g 160 -a unit -B "$( hold_arg )"
