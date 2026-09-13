#!/bin/sh
#
# Countdown from 2 with a 2.0s-per-digit default (honors -H)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown from 2"
expect "Giant digits counting 2, then 1"
expect "About ${BOSD_TEST_HOLD}s per digit (default 2.0; -H overrides)"
expect "Caption below reading: ${BOSD_TEST_HOLD}s-per-digit"
expect "Each digit is outlined and centered on the panel"
note "Always passes hold_seconds (unlike countdown_01)"
note "Digits tick in the background; ENTER kills and advances"

show_tick -a "${BOSD_TEST_HOLD}s-per-digit" -c 2 "$BOSD_TEST_HOLD"
