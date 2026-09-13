#!/bin/sh
#
# Countdown with gauge bar
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "countdown + gauge"
expect "Giant digits counting 3, then 2, then 1"
expect "Green gauge tick at ~55% in the bottom band through the count"
expect "Bar stays in its band; digits are centered above it"
note "Digits tick in the background; ENTER kills and advances"

show_tick -g 55 -B "$( hold_arg )" -c 3 $( countdown_hold_arg )
