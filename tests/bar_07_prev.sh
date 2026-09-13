#!/bin/sh
#
# Gauge previous-percent watermark (-P)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge previous watermark"
expect "Tall green ticks filled through ~30%"
expect "Short ticks between 30% and 80% stay full green (return zone)"
expect "Short ticks at/above the -P 80 watermark use a ~50% dimmer shade"
expect "Tall ticks are never dimmed"

show -g 30 -P 80 -B "$( hold_arg )"
