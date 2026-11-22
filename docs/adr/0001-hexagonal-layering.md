# 0001. Hexagonal layering

## Status

Accepted.

## Context

Hinkwise is a Qt Quick desktop application whose product is arithmetic: bucket
sizes, returns, phases and checks. The window is one way to reach that arithmetic,
not the arithmetic itself. Without a stated boundary, two failures are likely:

- business rules drift into QML bindings and event handlers, where they cannot be
  tested without a GUI and are invisible to anyone reading the C++;
- domain types start using Qt (`QString`, `QDate`, signals), so unit tests need an
  event loop and the licence and version of Qt become entangled with the model.

Both are cheap to prevent at the start and expensive to reverse later.

## Decision

The program is layered hexagonally: `domain`, `application`, `infrastructure` and
`presentation`. Dependencies point inwards, and the domain depends on nothing.

**`src/domain`** holds the model: value types, sizing tables, balance sheet,
metrics and rules. It is plain C++20. It does not include or link any Qt module, it
does not use Qt types, and it is built as the static library `hinkwise_domain`
(`CMakeLists.txt`), which links no libraries at all. Unit tests link that library
and `Qt6::Test` only, so they run headless and start in milliseconds.

**`src/application`** is for use cases: load a household, resolve targets, compute
metrics, evaluate rules, apply a change. It orchestrates domain objects and owns
transactions. It has no UI types.

**`src/infrastructure`** is where the outside world lives: persistence, settings,
the system clock, file formats, and the Qt classes that touch them.

**`src/presentation`** is view models and adapters for QML, together with `qml/`
itself.

QML carries no business logic. It binds to properties the presentation layer
exposes, formats them for the user's locale, and lays them out. It does not compute
a target, does not compare a balance against a band, does not decide what happens in
which phase, and does not hold model constants.

Only the first layer and the application shell exist today: `src/domain` plus
`src/main.cpp`, which sets up the QML engine. The other three layers are added by
the features that need them, and this record is the constraint those features must
fit.

## Consequences

- The model can be tested exhaustively without a display server, and the tests run
  in the same build on every platform.
- The model numbers can be asserted directly: changing a sizing table breaks a test,
  not a screenshot.
- The user interface can be replaced without touching a single rule, and Qt's
  licence, version and module set do not reach the domain.
- Values have to be converted at the boundary: `Money` becomes a formatted string in
  the presentation layer, `YearMonth` becomes a date, enums become labels. That is
  extra code, and it is the price of a domain that compiles without Qt.
- Domain code cannot use Qt conveniences. No `QString`, no `QDate`, no signals:
  strings are `std::string`, months are `YearMonth`, and errors are exceptions.
- The Qt-free rule is enforced by review, not by the compiler. `CMakeLists.txt`
  makes it visible, because `hinkwise_domain` has no Qt on its link line and no
  `find_package` result in its include path, but nothing stops a future edit from
  adding one.
- The current QML still hard-codes bucket descriptions and target strings as
  placeholder text. Under this decision those strings belong to the presentation of
  `BucketFacts`, and they move there before the skeleton becomes a screen.
