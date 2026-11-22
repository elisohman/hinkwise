#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Elis
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build hinkwise. Configure is idempotent, so running this repeatedly is cheap.
#
#   scripts/build.sh                 # debug build (preset: dev)
#   scripts/build.sh --run           # build, then launch the app
#   scripts/build.sh --test          # build, then run the test suite
#   scripts/build.sh -p release      # optimised build
#   scripts/build.sh --clean -p dev  # wipe build/dev and rebuild
#
# Works from any shell (fish, bash, zsh, Emacs compile) because it puts Qt's
# tools on PATH itself rather than relying on an interactive shell profile.

set -euo pipefail

cd "$(dirname "$0")/.."

# qml, qmllint, qmlformat, qt-cmake and qtpaths live here on Arch.
export PATH="/usr/lib/qt6/bin:${PATH}"

usage() {
    cat <<'EOF'
Build hinkwise. Configure is idempotent, so running it repeatedly is cheap.

  scripts/build.sh                 # debug build (preset: dev)
  scripts/build.sh --run           # build, then launch the app
  scripts/build.sh --test          # build, then run the test suite
  scripts/build.sh -p release      # optimised build
  scripts/build.sh -j 4            # limit parallel compile jobs
  scripts/build.sh --clean -p dev  # wipe build/dev and rebuild
  scripts/build.sh --install       # install to ~/.local (no root needed)
  scripts/build.sh --install --prefix /usr/local
  scripts/build.sh --uninstall     # reverse the last install
EOF
}

preset=dev
do_run=0
do_test=0
do_clean=0
do_install=0
do_uninstall=0
job_count=""
prefix="${HOME}/.local"

while (($#)); do
    case "$1" in
    -p | --preset)
        preset="${2:?--preset needs a value}"
        shift 2
        ;;
    -r | --run)
        do_run=1
        shift
        ;;
    -t | --test)
        do_test=1
        shift
        ;;
    -c | --clean)
        do_clean=1
        shift
        ;;
    -i | --install)
        do_install=1
        shift
        ;;
    -u | --uninstall)
        do_uninstall=1
        shift
        ;;
    --prefix)
        prefix="${2:?--prefix needs a value}"
        shift 2
        ;;
    -j)
        job_count="${2:?-j needs a number}"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        printf 'unknown option: %s\n\n' "$1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

case "$preset" in
dev | ci | release) ;;
*)
    printf 'unknown preset: %s (expected dev, ci or release)\n' "$preset" >&2
    exit 2
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
    printf 'cmake not found. Install it with:\n  sudo pacman -S --needed cmake ninja\n' >&2
    exit 1
fi

build_dir="build/${preset}"

# Uninstall needs no build, only the manifest CMake wrote last time.
if ((do_uninstall)); then
    manifest="${build_dir}/install_manifest.txt"
    if [[ ! -f "$manifest" ]]; then
        printf 'nothing to uninstall: %s not found\n' "$manifest" >&2
        exit 1
    fi
    printf '== uninstall ==\n'
    # CMake does not guarantee a trailing newline on the last manifest entry,
    # and `read` returns non-zero at EOF, so the final path would be skipped.
    while IFS= read -r installed || [[ -n "$installed" ]]; do
        if [[ -e "$installed" ]]; then
            rm -f "$installed"
            printf 'removed %s\n' "$installed"
        fi
    done <"$manifest"
    rm -f "$manifest"
    exit 0
fi

if ((do_clean)); then
    printf 'removing %s\n' "$build_dir"
    rm -rf "$build_dir"
fi

printf '== configure (%s) ==\n' "$preset"
cmake --preset "$preset" >/dev/null

printf '== build ==\n'
build_args=(--preset "$preset")
if [[ -n "$job_count" ]]; then
    build_args+=(-j "$job_count")
fi
cmake --build "${build_args[@]}"

if ((do_test)); then
    printf '\n== tests ==\n'
    ctest --preset "$preset" --output-on-failure
fi

if ((do_install)); then
    printf '\n== install (%s) ==\n' "$prefix"
    cmake --install "$build_dir" --prefix "$prefix"
fi

if ((do_run)); then
    printf '\n== run ==\n'
    exec "./${build_dir}/hinkwise"
fi

printf '\nbuilt %s/hinkwise\n' "$build_dir"