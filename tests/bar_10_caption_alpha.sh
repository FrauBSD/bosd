#!/bin/sh
#
# Gauge label opacity and outline (-A / -O / -o with -p / -a)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge captions translucent fill and outline"
expect "Tick bar at ~65% with washed-out fill (opacity 0.35)"
expect "Softer black outline on ticks (opacity 0.7)"
expect "Prefix L and append R use the same fill and outline alphas"
expect "With a compositor, desktop shows through ticks and labels"
note "Without a compositor the paint may look solid anyway"

show -A 0.35 -O 0.7 -g 65 -p L -a R -B "$( hold_arg )"

test_begin "gauge captions without outline"
expect "Tick bar at ~80% with no black halo on ticks"
expect "Prefix L and append R also without outline; fill edge only"
expect "Fully opaque green fill on ticks and labels"

show -o -g 80 -p L -a R -B "$( hold_arg )"

test_begin "gauge overage captions translucent"
expect "Full bar plus overage 125% at fill opacity 0.4"
expect "Append more just past overage at the same alphas"
expect "Outline opacity 0.5 on ticks, overage, and append"

show -A 0.4 -O 0.5 -g 125 -a more -B "$( hold_arg )"
