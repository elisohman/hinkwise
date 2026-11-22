#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Elis
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Everything CI will run, locally, in the order that fails fastest.
# Usage: scripts/check.sh

set -euo pipefail

cd "$(dirname "$0")/.."

step() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

step "configure"
cmake --preset dev >/dev/null

step "build"
cmake --build --preset dev

step "tests"
ctest --preset dev --output-on-failure

step "qml lint"
cmake --build build/dev --target all_qmllint

step "clang-format"
mapfile -t cxx < <(git ls-files '*.h' '*.cpp')
if ((${#cxx[@]})); then
    clang-format --dry-run --Werror "${cxx[@]}"
fi

step "ok"
