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

test_begin "screen word primary"
prim=$( xrandr --query 2> /dev/null | awk '/ connected primary/{ print $1; exit }' )
if [ "$prim" ]; then
	expect "Glyph on $prim; primary does not mean an output of that name"
	if ! "$BOSD" -D -S primary "$png" 0.2 > /dev/null 2>&1; then
		printf '%s\n' "bosd-test: -S primary failed" >&2
		exit 1
	fi
else
	expect "No glyph; no primary output is set"
	if "$BOSD" -D -S primary "$png" 0.2 > /dev/null 2>&1; then
		printf '%s\n' "bosd-test: -S primary was accepted" >&2
		exit 1
	fi
fi

test_begin "screen unknown output"
expect "No glyph; bosd reports that the output is not connected"

if "$BOSD" -D -S nosuch-output "$png" 0.2 > /dev/null 2>&1; then
	printf '%s\n' "bosd-test: unknown output was accepted" >&2
	exit 1
fi
