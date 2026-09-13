#!/bin/sh
#
# Large text and badge share typeface (-f with -b)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "large text badge face"
expect "Huge centered letters reading: OK in OCR A"
expect "Badge '1' at the upper-right also OCR A (same -f)"
expect "OCR A digit 1 is blocky/machine-like; compare against htext_05's plain sans 1"

show -b 1 -f 'OCR A' -T OK "$( hold_arg )"
