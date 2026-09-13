#!/bin/sh
#
# Large text with captions above and below, plus gauge
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text + captions + gauge"
expect "Huge letters reading: GO with caption above: over-text"
expect "Caption below reading: under-text"
expect "Green gauge tick at ~65% in the bottom band"
expect "Captions hug the large text; bar stays in its own band"

show -g 65 -B "$( hold_arg )" -p over-text -a under-text \
    -T GO "$( hold_arg )"
