#!/bin/sh
#
# Gauge with outline suppressed (-o)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "gauge without outline"
expect "Green tick at ~70% width"
expect "No black halo; fill edge only"
expect "Fully opaque fill"

show -o -g 70 -B "$( hold_arg )"
