# Hinkwise

A local-first desktop application for planning and evaluating personal finances
through the **four-bucket model** ([fyra-hinkar-modellen](https://rikatillsammans.se/spara-och-investera/fyra-hinkar-modellen/)) —
buffer, preserve-value, growth and play.

Built with C++20, Qt 6.8+ and QML. Data stays on your machine: no network code,
no telemetry, no accounts.

## Status

Working, and honest about its edges. Version 0.1.0.

What it does today: an onboarding wizard builds a household from a currency, a
phase, a monthly income split and a handful of accounts; the dashboard shows
years of freedom, the four buckets against their target bands, net worth with
housing and pension counted, what an equity crash would cost, and the rule
engine's coaching; cashflow tracks the 50/30/20 split over twelve months; the
plan page projects to financial independence and answers what-if questions; a
year-end review walks the model's three questions and ends in a rebalance
proposal; settings holds the phase switch and the jurisdiction constants.

What it does not do yet: importing a backup (export works and is lossless),
editing individual past months, and packaging for other distributions.

See `docs/model.md` for the rules it implements.

## Features

- **Four buckets, sized by the model** - buffer 0.5-6 months while accumulating
  and 1-5 years while preserving; preserve 25-35 annual expenses; play capped at
  10 % of growth in every phase.
- **Housing and pension are assets** - they sit in the preserve bucket, so net
  worth reflects what you own rather than only what is liquid.
- **Years of freedom** as the headline number instead of a balance.
- **Crash impact** - what a 50 % fall in growth would do to total net worth.
- **Insights, not nagging** - eleven rules, each saying what it sees, why the
  model cares, and what to do next.
- **Local only** - SQLite in `~/.local/share/elisohman/hinkwise/`, no network
  code, no account, no telemetry.

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

### Installing

There is no `install/` directory in the tree: CMake installs to a *prefix*, not
into the build folder. The default prefix is `/usr/local`, which needs root, so
for a personal install:

```sh
scripts/build.sh --install              # to ~/.local, no root
scripts/build.sh --install --prefix /usr/local
scripts/build.sh --uninstall            # reverses the last install
```

That installs four things:

```
~/.local/bin/hinkwise
~/.local/share/applications/io.github.elisohman.hinkwise.desktop
~/.local/share/icons/hicolor/scalable/apps/io.github.elisohman.hinkwise.svg
~/.local/share/metainfo/io.github.elisohman.hinkwise.metainfo.xml
```

The desktop entry and icon are what make the app appear in a launcher. The
desktop file's basename matches the app id the binary reports on Wayland
(`io.github.elisohman.hinkwise`), which is what lets a compositor such as niri
associate the window with the entry and apply window rules.

To stage a build without installing anywhere:

```sh
cmake --install build/dev --prefix /tmp/stage
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
packaging/      .desktop entry, icon and AppStream metadata for installs
scripts/        build.sh and check.sh, the local developer entry points
docs/           model notes, architecture decision records, licensing
```

## Documentation

- [`docs/model.md`](docs/model.md) — the four-bucket model as hinkwise implements it:
  the buckets, the two sizing tables, phase-dependent flow, annual expenses and the
  4 % rule, and how each part maps to the domain types.
- [`docs/adr/`](docs/adr/) — architecture decision records:
  [0001 hexagonal layering](docs/adr/0001-hexagonal-layering.md),
  [0002 money representation](docs/adr/0002-money-representation.md) and
  [0003 phase strategy](docs/adr/0003-phase-strategy.md).
- [`docs/licensing.md`](docs/licensing.md) — the GPL-3.0-or-later application licence
  and the LGPL-3.0-only obligations that come with Qt.

## Licence

Application code is **GPL-3.0-or-later**. It links dynamically against Qt 6, which is
**LGPL-3.0-only**. See `docs/licensing.md` for what that means if you fork this.

Hinkwise is an independent project. It is not affiliated with RikaTillsammans.
