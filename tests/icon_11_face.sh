#!/bin/sh
#
# Face for badge and captions (-f); -b required so -f is accepted
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "icon face"
expect "BSD wordmark with badge 'f' and caption below reading: serif-cap"
expect "Badge and caption use Courier (or the nearest Courier face)"
expect "Compare letterforms against the default DejaVu Sans cases"

show -b f -a serif-cap -f Courier "$png" "$( hold_arg )"
