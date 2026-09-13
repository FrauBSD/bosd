#!/bin/sh
#
# Shared helpers for the bosd visual test harness
#

if [ "$BOSD_TEST_LIB_LOADED" ]; then
	: already sourced
	return 0 2> /dev/null || exit 0
fi
BOSD_TEST_LIB_LOADED=1

#
# Locate ./bosd (or PATH), require DISPLAY, set hold default
#
bosd_test_init()
{
	local __bin __quiet=

	[ "$BOSD" ] && [ -x "$BOSD" ] && __quiet=1

	if [ ! "$BOSD_TEST_DIR" ]; then
		BOSD_TEST_DIR=$( cd "${0%/*}" && pwd ) || return
	fi
	BOSD_TEST_ROOT="${BOSD_TEST_DIR%/tests}"
	case "$BOSD_TEST_DIR" in
	*/tests) : ok ;;
	*)
		BOSD_TEST_ROOT=$( cd "$BOSD_TEST_DIR/.." && pwd ) || return
		;;
	esac

	if [ ! "$DISPLAY" ]; then
		printf '%s\n' \
		    "bosd-test: DISPLAY is unset (need an X11 session)" >&2
		return 1
	fi

	if [ -x "$BOSD_TEST_ROOT/bosd" ]; then
		BOSD="$BOSD_TEST_ROOT/bosd"
	elif [ -x "$BOSD" ]; then
		: keep caller-exported BOSD
	elif __bin=$( command -v bosd ) && [ -x "$__bin" ]; then
		BOSD="$__bin"
	else
		printf '%s\n' \
		    "bosd-test: no ./bosd in the tree and none on PATH" >&2
		printf '%s\n' \
		    "bosd-test: build the tree or install bosd(1)" >&2
		return 1
	fi

	# Default icon/text hold.  BOSD_TEST_HOLD_SET (from -H / env via
	# tests/run, or inferred here for a direct script invoke) means
	# countdown_hold_arg will pass hold_seconds.
	if [ "${BOSD_TEST_HOLD+set}" ]; then
		BOSD_TEST_HOLD_SET=1
	else
		BOSD_TEST_HOLD=2.0
	fi

	if [ ! "$__quiet" ]; then
		printf 'bosd-test: using %s (%s)\n' \
		    "$BOSD" "$( "$BOSD" -v )"
		printf 'bosd-test: hold=%ss DISPLAY=%s\n' \
		    "$BOSD_TEST_HOLD" "$DISPLAY"
		if [ "$BOSD_TEST_PAUSE" ]; then
			printf 'bosd-test: pause mode (ENTER advances)\n'
		fi
	fi
	return 0
}

expect()
{
	printf 'EXPECT: %s\n' "$*"
}

note()
{
	printf 'NOTE: %s\n' "$*"
}

test_begin()
{
	printf '\n==> %s\n' "$*"
}

show_stop()
{
	#
	# Tear down the background bosd from show().  kill $! as soon
	# as ENTER arrives; ignore a race where it already exited
	#
	kill $! 2> /dev/null || : already gone
	wait $! 2> /dev/null || : errors ignored
}

show_interrupted()
{
	show_stop
	exit 130
}

pause_for_enter()
{
	local __line

	printf 'Press ENTER for next case (Ctrl-C to abort): '
	if ! read -r __line; then
		exit 130
	fi
}

#
# Run bosd with -D (in-process, ignore daemon).
# With BOSD_TEST_PAUSE: hold is -1, paint in the background, wait for
# ENTER, then kill $!
#
show()
{
	printf 'RUN:'
	printf ' %s' "$BOSD" -D "$@"
	printf '\n'
	if [ ! "$BOSD_TEST_PAUSE" ]; then
		"$BOSD" -D "$@"
		return
	fi
	"$BOSD" -D "$@" &
	trap show_interrupted INT
	printf 'Press ENTER for next case (Ctrl-C to abort): '
	if ! read -r _; then
		show_interrupted
	fi
	trap - INT
	show_stop
}

#
# Foreground paint that must run to completion (e.g. -c ticking alone).
# In pause mode, wait for ENTER after it finishes
#
show_live()
{
	printf 'RUN:'
	printf ' %s' "$BOSD" -D "$@"
	printf '\n'
	"$BOSD" -D "$@" || return
	if [ "$BOSD_TEST_PAUSE" ]; then
		pause_for_enter
	fi
}

#
# Countdown (or other timed sequence) that must tick while ENTER is
# already offered.  Pause mode: monitor-mode background job + bg, then
# kill $! on ENTER.  Without pause: run in the foreground to completion
#
show_tick()
{
	printf 'RUN:'
	printf ' %s' "$BOSD" -D "$@"
	printf '\n'
	if [ ! "$BOSD_TEST_PAUSE" ]; then
		"$BOSD" -D "$@"
		return
	fi
	set -m
	"$BOSD" -D "$@" &
	bg %+ 2> /dev/null || : already running
	trap show_interrupted INT
	printf 'Press ENTER for next case (Ctrl-C to abort): '
	if ! read -r _; then
		show_interrupted
	fi
	trap - INT
	show_stop
}

hold_arg()
{
	#
	# Pause mode: indefinite hold so ENTER can kill $!.
	# Countdown must use countdown_hold_arg (-c rejects -1)
	#
	if [ "$BOSD_TEST_PAUSE" ]; then
		printf '%s' -1
	else
		printf '%s' "$BOSD_TEST_HOLD"
	fi
}

#
# Per-digit hold for -c only when -H / BOSD_TEST_HOLD was given.
# Otherwise print nothing so the caller passes no hold_seconds and
# bosd(1) keeps its 1s-per-digit default.  Never -1 (-c rejects it).
# Expand unquoted: $( countdown_hold_arg ) drops the word when empty.
#
countdown_hold_arg()
{
	if [ "$BOSD_TEST_HOLD_SET" ]; then
		printf '%s' "$BOSD_TEST_HOLD"
	fi
}

#
# Icon operand for glyph tests.  Prefer the packaged name "bsd" when
# share/bosd/bsd.png sits beside tests/ (ICONDIR).  Otherwise build or
# reuse examples/bsd.png in a source tree
#
example_png()
{
	local __var_to_set="$1" __png __py

	if [ -f "$BOSD_TEST_ROOT/bsd.png" ]; then
		eval $__var_to_set=\"bsd\"
		return 0
	fi

	if [ -f "$BOSD_TEST_ROOT/examples/bsd.py" ]; then
		__py="$BOSD_TEST_ROOT/examples/bsd.py"
		__png="$BOSD_TEST_ROOT/examples/bsd.png"
		if [ ! -f "$__png" ]; then
			note "building $__png"
			python3 "$__py" "$__png" || return
		fi
		if [ ! -f "$__png" ]; then
			printf 'bosd-test: missing %s\n' "$__png" >&2
			return 1
		fi
		eval $__var_to_set=\"\$__png\"
		return 0
	fi

	printf '%s\n' "bosd-test: cannot find bsd glyph (install or make example)" >&2
	return 1
}
