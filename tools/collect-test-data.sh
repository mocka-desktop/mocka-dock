#!/bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2026 The Mocka Desktop Project
#
# Records the X properties the matcher uses (SPEC section 6) for one window,
# and the fields of the desktop entry it should match, into docs/test-data/.
# Window titles are never recorded.
#
# Usage: collect-test-data.sh [-i window-id] [-s] name desktop-id
#
#   name        file name for the sample, for example "firefox"
#   desktop-id  expected desktop entry ID, for example "firefox.desktop",
#               or "none" when the window should use the fallback
#   -i id       use this window instead of clicking one
#   -s          keep watching WM_CLASS and record every change until Ctrl+C

set -eu

usage()
{
	echo "usage: $0 [-i window-id] [-s] name desktop-id" >&2
	exit 1
}

id=""
spy=0
while getopts i:s opt; do
	case "$opt" in
	i) id="$OPTARG" ;;
	s) spy=1 ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ $# -eq 2 ] || usage

name="$1"
expect="$2"
# Keep the home directory and host name out of the samples. On FreeBSD
# /home links to /usr/home, and process paths use the resolved form.
scrub="s#/usr$HOME#/home/user#g; s#$HOME#/home/user#g; s#$(hostname)#host#g"
dir="$(dirname "$0")/../docs/test-data"
out="$dir/$name.txt"
props="WM_CLASS _NET_STARTUP_ID WM_WINDOW_ROLE _NET_WM_WINDOW_TYPE"

if [ -z "$id" ]; then
	echo "Click the window to record." >&2
	id=$(xdotool selectwindow)
fi

{
	echo "# expect: $expect"
	# shellcheck disable=SC2086
	xprop -id "$id" -notype $props
	# GTK apps set the startup ID on the client leader window instead.
	leader=$(xprop -id "$id" WM_CLIENT_LEADER | awk '/window id/ { print $NF }')
	if [ -n "$leader" ] && [ "$leader" != "$id" ]; then
		printf 'leader '
		xprop -id "$leader" -notype _NET_STARTUP_ID
	fi
	# The executable of the window's process (SPEC section 6, step 5).
	pid=$(xprop -id "$id" _NET_WM_PID | awk '/=/ { print $NF }')
	if [ -n "$pid" ]; then
		printf 'executable = '
		procstat -b "$pid" | tail -n 1 | sed -E 's/^ *[0-9]+ +[^ ]+ +[0-9]+ +//'
	fi
} | sed "$scrub" > "$out"
echo "wrote $out" >&2

# Copy only the desktop entry fields the matcher reads.
if [ "$expect" != "none" ]; then
	for d in "${XDG_DATA_HOME:-$HOME/.local/share}" /usr/local/share /usr/share; do
		src="$d/applications/$expect"
		[ -f "$src" ] || continue
		awk '
			/^\[/ { in_entry = ($0 == "[Desktop Entry]") }
			in_entry && /^(\[Desktop Entry\]|(Type|Name|Exec|TryExec|Icon|StartupWMClass|NoDisplay)=)/
		' "$src" | sed "$scrub" > "$dir/applications/$expect"
		echo "wrote $dir/applications/$expect" >&2
		break
	done
fi

if [ "$spy" -eq 1 ]; then
	echo "Watching WM_CLASS, press Ctrl+C to stop." >&2
	echo "# WM_CLASS changes:" >> "$out"
	xprop -id "$id" -notype -spy WM_CLASS | while read -r line; do
		echo "# $line" | sed "$scrub" >> "$out"
	done
fi
