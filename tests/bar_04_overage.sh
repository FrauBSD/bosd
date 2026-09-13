#!/bin/sh
#
# Gauge over 100%: full ticks plus overage label
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge overage label"
expect "Bottom gauge band fully filled (every tick tall)"
expect "Overage text just past the bar's right edge reading: 153%"
expect "Label uses the same green as the ticks, with black outline"

show -g 153 -B "$( hold_arg )"
