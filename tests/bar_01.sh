#!/bin/sh
#
# Opaque gauge bar at mid fill
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge opaque mid-fill"
expect "A green tick bar near the panel bottom"
expect "Fill around 40% of the bar width"
expect "Black outline around the tick; fully opaque"
if [ "$BOSD_TEST_PAUSE" ]; then
	expect "Bar stays until ENTER kills the background bosd"
else
	expect "Bar holds for ${BOSD_TEST_HOLD}s then vanishes"
fi

show -g 40 -B "$( hold_arg )"
