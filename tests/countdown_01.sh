#!/bin/sh
#
# Countdown digits (-c)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown"
expect "Giant digits counting 3, then 2, then 1"
expect "About ${BOSD_TEST_HOLD}s per digit (hold from -H / BOSD_TEST_HOLD)"
expect "Each digit is outlined and centered on the panel"
note "Countdown runs to completion (not held for ENTER mid-tick)"

# Must tick live; pause mode waits after 1, not during
show_live -c 3 "$( countdown_hold_arg )"
