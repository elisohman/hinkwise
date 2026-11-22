# Hinkwise

A local-first desktop application for planning and evaluating personal finances
through the **four-bucket model** ([fyra-hinkar-modellen](https://rikatillsammans.se/spara-och-investera/fyra-hinkar-modellen/)) —
buffer, preserve-value, growth and play.

Built with C++20, Qt 6.8+ and QML. Data stays on your machine: no network code,
no telemetry, no accounts.

## Status

Skeleton. The window, theme and bucket cards exist; there is no data model yet.
See `docs/model.md` for the domain rules the application is being built against.

## Building

Requires Qt 6.8 or newer (`qt6-base`, `qt6-declarative`), CMake 3.21+, Ninja and a C++20 compiler.

The short version, from a clean checkout:

```sh
scripts/build.sh --run     # configure, build, launch
scripts/check.sh           # everything CI runs: build, tests, qmllint, formatting
```

`scripts/build.sh` takes `--preset dev|release`, `--run`, `--test`, `--clean` and `-j N`.
It puts Qt's tools on `PATH` itself, so it works from fish, bash, or an editor's
compile command without depending on your shell profile.

Underneath it is plain CMake:

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/hinkwise
```

QML linting runs through CMake, which sets up the module import paths for you:

```sh
cmake --build build/dev --target all_qmllint
```

Tests (as they appear):

```sh
ctest --preset dev
```

## Layout

```
src/            C++ sources; domain, application, infrastructure, presentation
qml/            QML: Main.qml, theme/, components/, and later pages/
tests/          domain unit tests and QML tests
scripts/        build.sh and check.sh, the local developer entry points
docs/           model notes, architecture decision records, licensing
```

## Licence

Application code is **GPL-3.0-or-later**. It links dynamically against Qt 6, which is
**LGPL-3.0-only**. See `docs/licensing.md` for what that means if you fork this.

Hinkwise is an independent project. It is not affiliated with RikaTillsammans.
