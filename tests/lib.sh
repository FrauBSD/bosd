#!/bin/sh
#
# Shared helpers for the bosd visual test harness
#

if [ "$BOSD_TEST_LIB_LOADED" ]; then
	: already sourced
	return 0 2> /dev/null || exit 0
fi
BOSD_TEST_LIB_LOADED=1

# Dedicated channel when tests/run -d warms a singular daemon
BOSD_TEST_INSTANCE="${BOSD_TEST_INSTANCE:-bosd-test}"

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
		if [ "$BOSD_TEST_DAEMON" ]; then
			printf 'bosd-test: daemon mode (-n %s)\n' \
			    "$BOSD_TEST_INSTANCE"
		fi
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

#
# Invoke bosd: -D when alone, or -n $BOSD_TEST_INSTANCE against the
# warm harness daemon (tests/run -d)
#
bosd_cli()
{
	if [ "$BOSD_TEST_DAEMON" ]; then
		"$BOSD" -n "$BOSD_TEST_INSTANCE" "$@"
	else
		"$BOSD" -D "$@"
	fi
}

bosd_cli_clear()
{
	[ "$BOSD_TEST_DAEMON" ] || return 0
	"$BOSD" -n "$BOSD_TEST_INSTANCE" -C 2> /dev/null || : clear failed
}

#
# After a daemon handoff the client returns at once; sleep so the
# paint can be seen, then clear the channel for the next case
#
bosd_daemon_settle()
{
	local __secs="$1"

	[ "$BOSD_TEST_DAEMON" ] || return 0
	if [ "$BOSD_TEST_PAUSE" ]; then
		pause_for_enter
		bosd_cli_clear
		return 0
	fi
	[ "$__secs" ] || __secs="$BOSD_TEST_HOLD"
	sleep "$__secs"
	bosd_cli_clear
}

show_stop()
{
	#
	# Tear down the background bosd from show() in -D pause mode.
	# kill $! as soon as ENTER arrives; ignore a race where it
	# already exited
	#
	kill $! 2> /dev/null || : already gone
	wait $! 2> /dev/null || : errors ignored
}

show_interrupted()
{
	if [ "$BOSD_TEST_DAEMON" ]; then
		bosd_cli_clear
	else
		show_stop
	fi
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
# Start the singular harness daemon on BOSD_TEST_INSTANCE
#
bosd_daemon_start()
{
	local __i= __sock= __uid=

	[ "$BOSD_TEST_DAEMON" ] || return 0
	__uid=$( id -u ) || return
	__sock="/tmp/bosd.${BOSD_TEST_INSTANCE}.${__uid}.sock"
	bosd_cli_clear
	if [ "$BOSD_TEST_DAEMON_PID" ]; then
		kill "$BOSD_TEST_DAEMON_PID" 2> /dev/null || : already gone
		wait "$BOSD_TEST_DAEMON_PID" 2> /dev/null || : errors ignored
		BOSD_TEST_DAEMON_PID=
	fi
	rm -f "$__sock" \
	    "/tmp/bosd.${BOSD_TEST_INSTANCE}.${__uid}.pid" \
	    2> /dev/null || : stale ipc
	"$BOSD" -n "$BOSD_TEST_INSTANCE" -d &
	BOSD_TEST_DAEMON_PID=$!
	export BOSD_TEST_DAEMON_PID
	__i=0
	while [ "$__i" -lt 50 ]; do
		if [ -S "$__sock" ] && kill -0 "$BOSD_TEST_DAEMON_PID" \
		    2> /dev/null; then
			note "warmed channel -n $BOSD_TEST_INSTANCE (pid $BOSD_TEST_DAEMON_PID)"
			return 0
		fi
		sleep 0.1
		__i=$(( $__i + 1 ))
	done
	printf '%s\n' \
	    "bosd-test: daemon -n $BOSD_TEST_INSTANCE failed to start" >&2
	return 1
}

bosd_daemon_stop()
{
	[ "$BOSD_TEST_DAEMON" ] || return 0
	bosd_cli_clear
	if [ "$BOSD_TEST_DAEMON_PID" ]; then
		kill "$BOSD_TEST_DAEMON_PID" 2> /dev/null || : already gone
		wait "$BOSD_TEST_DAEMON_PID" 2> /dev/null || : errors ignored
		BOSD_TEST_DAEMON_PID=
	fi
}

#
# Run bosd (daemon channel or -D).  With BOSD_TEST_PAUSE and -D: hold
# -1 in the background, wait for ENTER, kill $!.  With a warm daemon:
# hand off, wait for ENTER or settle, then -C
#
show()
{
	printf 'RUN:'
	if [ "$BOSD_TEST_DAEMON" ]; then
		printf ' %s' "$BOSD" -n "$BOSD_TEST_INSTANCE" "$@"
	else
		printf ' %s' "$BOSD" -D "$@"
	fi
	printf '\n'
	if [ "$BOSD_TEST_DAEMON" ]; then
		bosd_cli "$@" || return
		bosd_daemon_settle
		return
	fi
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
# In pause mode, wait for ENTER after it finishes.  Daemon mode: hand
# off and settle (countdown settle is longer; see show_tick)
#
show_live()
{
	printf 'RUN:'
	if [ "$BOSD_TEST_DAEMON" ]; then
		printf ' %s' "$BOSD" -n "$BOSD_TEST_INSTANCE" "$@"
	else
		printf ' %s' "$BOSD" -D "$@"
	fi
	printf '\n'
	if [ "$BOSD_TEST_DAEMON" ]; then
		bosd_cli "$@" || return
		bosd_daemon_settle
		return
	fi
	"$BOSD" -D "$@" || return
	if [ "$BOSD_TEST_PAUSE" ]; then
		pause_for_enter
	fi
}

#
# Countdown (or other timed sequence) that must tick while ENTER is
# already offered.  Pause+-D: monitor-mode background job + bg, then
# kill $! on ENTER.  Daemon: hand off, settle long enough for ticks,
# then -C.  Without pause or daemon: run in the foreground
#
show_tick()
{
	printf 'RUN:'
	if [ "$BOSD_TEST_DAEMON" ]; then
		printf ' %s' "$BOSD" -n "$BOSD_TEST_INSTANCE" "$@"
	else
		printf ' %s' "$BOSD" -D "$@"
	fi
	printf '\n'
	if [ "$BOSD_TEST_DAEMON" ]; then
		bosd_cli "$@" || return
		# Cover long -c runs (e.g. 25 x 0.1s) and default 3-digit
		bosd_daemon_settle "${BOSD_TEST_TICK_SETTLE:-12}"
		return
	fi
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
	# Pause mode: indefinite hold so ENTER can kill $! (or -C the
	# channel).  Countdown must use countdown_hold_arg (-c rejects -1)
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
