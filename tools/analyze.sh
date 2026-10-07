#!/bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2026 The Mocka Desktop Project
#
# Runs the Clang static analyser over every source file, using the flags the
# build actually uses. scan-build is not packaged on FreeBSD, so this drives
# clang --analyze from the build's compile_commands.json instead.
#
# Usage: tools/analyze.sh [builddir]        (default: build)

set -eu

builddir="${1:-build}"
commands="$builddir/compile_commands.json"

if [ ! -f "$commands" ]; then
	echo "no $commands: run 'meson setup $builddir' first" >&2
	exit 1
fi

python3 - "$commands" <<'PY'
import json, os, shlex, subprocess, sys

commands = json.load(open(sys.argv[1]))
analysed = 0
failed = 0
seen = set()

for entry in commands:
    source = entry["file"]
    if "/src/" not in source and "/tests/" not in source:
        continue
    if source in seen:
        continue
    seen.add(source)

    # Drop the output, the compile-only flag and the dependency-file
    # machinery, keeping every flag that changes how the code is read.
    takes_argument = ("-o", "-MF", "-MQ", "-MT")
    args = []
    skip = False
    for arg in shlex.split(entry["command"]):
        if skip:
            skip = False
            continue
        if arg in takes_argument:
            skip = True
            continue
        if arg == "-c" or arg.startswith("-M") or arg.endswith(".o"):
            continue
        args.append(arg)
    args += ["--analyze", "-Xanalyzer", "-analyzer-output=text"]

    result = subprocess.run(args, cwd=entry["directory"],
                            capture_output=True, text=True)
    analysed += 1
    output = (result.stdout + result.stderr).strip()
    if output:
        failed += 1
        print(f"===== {os.path.relpath(source)} =====")
        print(output)

print(f"\n{analysed} files analysed, {failed} with findings")
sys.exit(1 if failed else 0)
PY
