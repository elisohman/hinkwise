# 0002. Money is integer minor units with an explicit currency

## Status

Accepted.

## Context

The application adds, subtracts, compares and scales household amounts, and splits a
monthly contribution across buckets. Those operations must be exact: a plan whose
parts do not add up to the whole is wrong even if it is off by one öre, and repeated
rounding turns small errors into visible ones. Percentages matter just as much,
because every target is a percentage of expenses.

The obvious alternatives are all worse here:

- `double` cannot represent 0.1 exactly, and comparing two accumulated amounts for
  equality becomes a question about epsilon rather than about money.
- A decimal or arbitrary-precision type means a dependency, and the domain is
  deliberately Qt-free (`0001`), so nothing from Qt is available either. Fixed-point
  in `int64` is the same idea without the dependency.
- An unstructured string or a bare integer without a currency invites mixing kronor
  with euros, which is a mistake that only shows up as a wrong number.

## Decision

**Money is `int64` minor units plus a `Currency` enum.** `Money` (`src/domain/money.h`)
stores öre, cents or the local equivalent, together with a `Currency` value
(`Sek`, `Eur`, `Usd`). `CurrencyTraits` carries the ISO code and the number of minor
digits per currency, so formatting has one place to look. The enum is deliberate: a
string could hold a typo, an enum cannot, and every code path that touches money has
to agree on it.

**Percentages are `int32` basis points**, 10 000 bp to the whole
(`src/domain/percentage.h`). No floating point is used for ratios either.

**Arithmetic is same-currency only.** Mixing currencies throws
`std::invalid_argument` rather than converting, because the domain has no exchange
rates and a silent conversion would encode a rate decision nobody made. Money that
must be created from nothing is created in a stated currency (`Money::zero`), and
`TargetInputs` has no default constructor so a euro household cannot inherit a
kronor zero.

**Rounding of value types is half away from zero.** The rule is applied wherever an
amount is scaled or a metric ratio is computed:

- `Money::scaledBy` multiplies by basis points, adds or subtracts 5 000 according to
  the sign, then divides by 10 000;
- `Percentage::fromPercent` uses `std::lround`;
- the ratio helper in `metrics.cpp` applies the same rule to numerator and
  denominator.

Half away from zero, not half to even: a half öre should round in the same direction
on both sides of zero, so a loss and a gain of the same size stay symmetric.

**Splitting preserves the sum.** `Money::splitShare(index, parts)` divides, then
hands the remainder out one minor unit at a time, so the shares always reconstruct
the original exactly. When a contribution is spread across buckets, no öre is lost
and none is invented.

**Formatting happens at the edge.** `Money::toString` exists for logs and tests only.
Formatting for people is the presentation layer's job, with `QLocale`, so the domain
never needs to know a locale, a currency symbol or a thousands separator.

**Overflow is bounded and accepted.** `scaledBy` multiplies two integers in `int64`.
The largest multiplier the model uses is x35, that is 350 000 basis points, so it
overflows once the amount exceeds roughly 2.6 × 10^13 minor units, about 263 billion
SEK of annual expenses at the top of the preserving band. Every other multiplication
in the domain has a smaller factor: 12 for annual expenses, 10 000 bp for ratios and
at most 33 for the FI targets, so that is the tightest bound. It is far outside any
household's range, so it is documented rather than guarded: a checked multiply would
add a branch and an error path to every scaling, and would stop `Money` being
`constexpr` and `noexcept`, to protect against an input that cannot occur. If the
domain ever handles national-scale amounts, or a jurisdiction multiplier above 35,
this decision is the one to revisit.

## Consequences

- Sums reconcile. A plan, its parts and its rounding are all exact, and no comparison
  needs a tolerance.
- Currency mistakes surface as an exception at the boundary instead of as a plausible
  but wrong figure.
- `Money` and `Percentage` stay `constexpr` and cheap to copy, which is why they can
  appear in `constexpr` model tables.
- Money has two decimal places, no more. A share price of 123.456 SEK cannot be
  represented, and that is intended: money is an amount, not a quotation. A price
  type would be a separate decision.
- Adding a currency means editing the enum and `CurrencyTraits`. All three current
  currencies have two minor digits, so a zero-decimal currency such as JPY is not
  supported yet.
- Rates are resolved to one basis point, 0.01 percentage points, which is finer than
  any return, tax drag or withdrawal rate the model uses.
- An out-of-range input is undefined behaviour in principle, because signed integer
  overflow is not checked. The bound above is the accepted answer to that, not an
  accident.
