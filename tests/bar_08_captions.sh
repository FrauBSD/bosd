#!/bin/sh
#
# Gauge-alone -p / -a captions (seats with and without overage)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge prefix only"
expect "Green tick bar near the panel bottom at ~50% fill"
expect "Prefix Hi right-justified just left of the bar"
expect "No text past the bar's right edge"
expect "Prefix matches tick height, green fill, black outline"
expect "Prefix centerline aligns with the bar centerline"

show -g 50 -p Hi -B "$( hold_arg )"

test_begin "gauge append only under 100%"
expect "Green tick bar at ~60% fill"
expect "Append Lo left-justified just past the bar's right edge"
expect "Append sits where overage text would be"
expect "Same face, size, color, and outline as overage would use"

show -g 60 -a Lo -B "$( hold_arg )"

test_begin "gauge prefix and append under 100%"
expect "Green tick bar at ~72% fill"
expect "Prefix Vol right-justified just left of the bar"
expect "Append dB left-justified just past the bar's right edge"
expect "Both labels match the tick height, green fill, black outline"
expect "Label centerlines align with the bar centerline"

show -g 72 -p Vol -a dB -B "$( hold_arg )"

test_begin "gauge append with overage"
expect "Full bar plus overage text 142% past the right edge"
expect "Append dB left-justified just past the overage text"
expect "Overage and append share face, size, color, and outline"

show -g 142 -a dB -B "$( hold_arg )"

test_begin "gauge captions with overage"
expect "Full bar plus overage text 142% past the right edge"
expect "Prefix Vol still right-justified left of the bar"
expect "Append dB left-justified just past the overage text"
expect "Prefix, overage, and append share face, size, and alphas"

show -g 142 -p Vol -a dB -B "$( hold_arg )"
