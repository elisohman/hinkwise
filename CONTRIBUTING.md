<!--
SPDX-FileCopyrightText: 2026 Elis
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing

## Before you push

Run the whole gate locally:

```sh
scripts/check.sh
```

It configures, builds, runs the tests, runs `all_qmllint` and checks formatting, in the
order that fails fastest. CI runs the same checks on an unattended Ubuntu 24.04 runner
with Qt 6.8.3 (the `ci` preset, so Debug and `QT_QPA_PLATFORM=offscreen`), so anything
green locally should be green there.

## Formatting

The formatter is pinned: a clang-format that differs from the pinned one will disagree
with CI. The pinned version lives in `.clang-format-version` and is installed from PyPI:

```sh
pip install "clang-format==$(cat .clang-format-version)"
```

CI runs `clang-format --dry-run --Werror` over every tracked `.h` and `.cpp` file. Format
your changes with that same version before committing:

```sh
git ls-files '*.h' '*.cpp' | xargs clang-format -i
```

## Licensing

Application code is GPL-3.0-or-later and every file carries SPDX headers; CI runs
`reuse lint` over the tree.

Qt is LGPL-3.0-only and is linked **dynamically**. Never link Qt statically — a static
build turns the LGPL into a relinking obligation that requires shipping object files.
See `docs/licensing.md` for the full set of obligations.
