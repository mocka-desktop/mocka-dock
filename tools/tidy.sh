#!/bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2026 The Mocka Desktop Project
#
# Runs clang-tidy over every source file, with the checks in .clang-tidy.
# It catches what the static analyser cannot, reference handling and API
# misuse rather than control flow, so the two are worth running together.
#
# clang-tidy is not in the FreeBSD packages: devel/llvm<N> has to be built
# with the EXTRAS option. The binary is named after its version there, so
# clang-tidy19 is tried before the plain name.
#
# Usage: tools/tidy.sh [builddir]        (default: build)

set -eu

builddir="${1:-build}"

if [ ! -f "$builddir/compile_commands.json" ]; then
	echo "no $builddir/compile_commands.json: run 'meson setup $builddir' first" >&2
	exit 1
fi

tidy=""
for candidate in clang-tidy19 clang-tidy20 clang-tidy21 clang-tidy; do
	if command -v "$candidate" >/dev/null 2>&1; then
		tidy="$candidate"
		break
	fi
done

if [ -z "$tidy" ]; then
	echo "clang-tidy not found: build devel/llvm19 with the EXTRAS option" >&2
	exit 1
fi

# clang-tidy exits 0 whatever it finds, so anything it prints is a finding,
# and it exits non-zero only when it could not check a file at all. Every
# file also reports how many warnings the system headers raised; those are
# left out by HeaderFilterRegex, so that count line is not a finding.
status=0
for source in src/*.c tests/*.c; do
	[ -f "$source" ] || continue
	if found=$("$tidy" -p "$builddir" --quiet "$source" 2>&1); then
		code=0
	else
		code=$?
	fi
	found=$(printf '%s\n' "$found" | grep -Ev '^[0-9]+ warnings? generated\.$' || true)
	if [ -n "$found" ]; then
		printf '%s\n' "$found"
		status=1
	fi
	if [ "$code" -ne 0 ]; then
		echo "clang-tidy could not check $source (exit $code)" >&2
		status=1
	fi
done

if [ "$status" -eq 0 ]; then
	echo "clang-tidy: clean"
fi

exit "$status"