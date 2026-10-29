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

test_begin "small text literal newline"
expect "Two caption lines near the panel bottom"
expect "First line reads: line-one"
expect "Second line reads: line-two (stacked below the first)"
expect "A literal newline in the -t operand splits the lines"

show -t 'line-one
line-two' "$( hold_arg )"
