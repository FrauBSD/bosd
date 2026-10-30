#
# -S screen: one named output, or all of them
#

BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || exit
. "$BOSD_TEST_DIR/lib.sh" || exit
bosd_test_init || exit
example_png png || exit

test_begin "screen all"
expect "A copy of the glyph on every connected output"
expect "Each copy is centered on that output and sized to its panel"
expect "On a single panel this matches the primary placement"

show -S all "$png" "$( hold_arg )"

out=$( xrandr --query 2> /dev/null | awk '/ connected/{ print $1; exit }' )
if [ "$out" ]; then
	test_begin "screen named $out"
	expect "Glyph centered on $out"
	expect "No copy on any other output"

	show -S "$out" "$png" "$( hold_arg )"
fi

test_begin "screen unknown output"
expect "No glyph; bosd reports that the output is not connected"

if "$BOSD" -D -S nosuch-output "$png" 0.2 > /dev/null 2>&1; then
	printf '%s\n' "bosd-test: unknown output was accepted" >&2
	exit 1
fi
