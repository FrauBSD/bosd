#!/bin/sh
#
# Countdown with captions above and below, plus gauge
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown + captions + gauge"
expect "Digits 3-2-1 with caption above reading: over-count"
expect "Caption below reading: under-count"
expect "Green gauge tick at ~50% in the bottom band through the count"
note "Digits tick in the background; ENTER kills and advances"

show_tick -g 50 -B "$( hold_arg )" -p over-count -a under-count \
    -c 3 $( countdown_hold_arg )
