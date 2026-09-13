#!/bin/sh
#
# Small caption with embedded space (\x20)
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit

test_begin "small text space escape"
expect "One caption line near the panel bottom"
expect "Text reads: hello world (with a real space)"
expect "Embedded \\x20 in the -t operand becomes the space"
expect "Literal whitespace in the -t operand is rejected; escapes required"

show -t 'hello\x20world' "$( hold_arg )"
