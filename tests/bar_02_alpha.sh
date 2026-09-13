#!/bin/sh
#
# Gauge fill and outline translucency (-A / -O)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge translucent fill and outline"
expect "Same bottom gauge band as the opaque case"
expect "Fill at ~55% width, but washed out (fill opacity 0.35)"
expect "Outline still visible but softer (outline opacity 0.7)"
expect "With a compositor, desktop shows through the tick"
note "Without a compositor the bar may look solid anyway"

show -A 0.35 -O 0.7 -g 55 -B "$( hold_arg )"
