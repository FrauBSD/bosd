#!/bin/sh
#
# Small caption with embedded newline (\x0a)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text newline escape"
expect "Two caption lines near the panel bottom"
expect "First line reads: line-one"
expect "Second line reads: line-two (stacked below the first)"
expect "Embedded \\x0a in the -t operand becomes the line break"

show -t 'line-one\x0aline-two' "$( hold_arg )"
