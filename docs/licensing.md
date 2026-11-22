# Licensing

Hinkwise combines two licences. They are compatible, but they are not the same.

| Component | Licence | Notes |
|---|---|---|
| Hinkwise application code (`src/`, `qml/`, `tests/`, docs) | GPL-3.0-or-later | `LICENSES/GPL-3.0-or-later.txt` |
| Qt 6 (Core, Gui, Quick, QuickControls2, Sql, Test) | LGPL-3.0-only | not redistributed; see below |

## Why GPL-3.0-or-later for the application

LGPLv3 is "GPLv3 plus additional permissions". Choosing GPLv3 for our own code means
every LGPL obligation is automatically satisfied, and we can never accidentally paint
ourselves into a corner by wanting a GPL-only Qt module later (for example Qt Charts).

## Obligations that come from Qt

1. **Dynamic linking only.** Qt is linked as shared libraries. The build must not link
   Qt statically — a static build would turn Qt's LGPL into a relinking obligation that
   requires shipping object files.
2. **Keep the notices.** Hinkwise does not redistribute Qt: it links dynamically against
   the Qt libraries the system already provides, so Qt's own licence text ships with Qt
   (`/usr/share/licenses/qt6-base/`), not with this repository. The in-app About screen
   identifies Qt and its version. If a future release ever bundles Qt — an AppImage, for
   instance — the LGPL-3.0 text must be added back under `LICENSES/` and shipped with the
   bundle.
3. **Allow replacement.** Do not prevent a user from substituting their own build of the
   Qt libraries. Dynamic linking satisfies this as long as nothing (code signing,
   sandboxing, adversarial linker flags) blocks it.
4. **State changes.** Modified Qt sources, if any were ever vendored, must be documented.
   Hinkwise does not vendor or modify Qt.

## If you fork this

You may redistribute Hinkwise under GPL-3.0-or-later. If you add a GPL-only Qt module,
that is fine. If you want to ship under a different licence, the LGPL terms on Qt still
apply — the four obligations above remain, independently of the application's licence.
