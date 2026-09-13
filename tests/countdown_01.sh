#!/bin/sh
#
# Countdown digits (-c)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown"
expect "Giant digits counting 3, then 2, then 1"
if [ "$BOSD_TEST_HOLD_SET" ]; then
	expect "About ${BOSD_TEST_HOLD}s per digit (-H / BOSD_TEST_HOLD)"
else
	expect "About 1s per digit (bosd default; pass -H to override)"
fi
expect "Each digit is outlined and centered on the panel"
note "Digits tick in the background; ENTER kills and advances"

show_tick -c 3 $( countdown_hold_arg )
