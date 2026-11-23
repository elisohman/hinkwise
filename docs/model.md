# The four-bucket model

Hinkwise is built around the four-bucket model, *fyra-hinkar-modellen*: a way of
sorting a household's wealth into four parts, each with its own purpose and its
own spending horizon. It is not a budgeting method. It does not say what to spend
on groceries this month. It says what job each part of the wealth has, how large
each part should be, and which way money should move between them.

Each bucket is defined twice over: by *when* the money is needed (the horizon) and
by *what it protects against* (the purpose). The buffer absorbs shocks and
opportunities inside three years. Preserve value keeps purchasing power over three
to nine years and refills the buffer. Growth is the engine for money that will not
be touched for a decade or more. Play is deliberate, capped speculation, so that
the other three buckets stay untouched.

Two consequences follow from that structure, and both matter:

- the sizes of the buckets are bands relative to what the household spends, not fixed
  amounts of kronor, so the same rules fit every income;
- the asset mix that counts is the whole household's, not only the money in funds.

## The four buckets

The facts below are the ones the code stores in `BucketFacts` (`src/domain/bucket.cpp`).
Returns are the model's long-run averages, not forecasts. "Real" means after
inflation.

| Bucket | Purpose | Horizon | Nominal return | Real return | KIID risk | Example assets | Debts |
|---|---|---|---|---|---|---|---|
| Buffer | Absorb shocks, seize opportunities, fund spending within three years | 0–3 years | 0–2 % | -1 to 0 % | 1–2, low | Bank account with a deposit guarantee | "Bad" loans: anything priced above a mortgage |
| Preserve value | Keep pace with inflation, smooth out market swings, refill the buffer | 3–9 years | 3–6 % | 2–3 % | 3–4, medium-high | Home, other real estate, mixed equity and bond portfolios, traditional life insurance, income pension | "Good" loans: mortgage, student loan, securities credit |
| Growth | Make the money you will live on later; the engine of the economy | 10+ years | 6–9 % | 5–7 % | 5–6, high | Global equity index funds, occupational pension, premium pension | None in the model |
| Play | Give the emotional part of investing somewhere to go, so the other buckets stay untouched | Any | — | — | 1–7, the whole scale | Single shares, crypto, speculation | None in the model |

The KIID band is the 1–7 risk scale used on European fund documents; a bucket's
band is the range a sensible fund for that horizon sits in.

Play has no expected return in the model. The code expresses that as a zero
percentage range and the full 1–7 risk band, and the user interface is meant to
render the return as an em dash rather than "0 %". A zero would be a claim the
model does not make.

Debts are placed by the model, not by hinkwise. A loan priced above a typical
mortgage rate is expensive enough that paying it down beats investing, so the
model groups it with the buffer; mortgages and student loans buy something that
lasts and are cheap, so they sit with preserve value. Hinkwise keeps one list of
debts and decides "bad" with `isBadLoan`, which is strict: a rate equal to the
benchmark is not bad, only one above it.

## Two sizing tables

The model states two sizing tables, so the code has exactly two, both in
`src/domain/phase.cpp`. Which table applies follows from the phase, not from the
household's amounts.

| Bucket | Accumulating phases (getting started, building wealth) | Preserving phases (preserving, financially independent) |
|---|---|---|
| Buffer | 0.5–6 months of expenses, plus lump sums due within three years | 3 ± 2 annual expenses, that is 1–5, plus lump sums due within three years |
| Preserve value | As small as possible: no target to fill | 25–35 annual expenses |
| Growth | As large as possible until the goal is reached | Optional: no target |
| Play | Optional, but never more than 10 % of growth, excluding pension | Optional, same 10 % cap |

**Why the accumulating buffer is so wide.** The low end suits a household with two
secure incomes; the high end suits one with a single variable income, or a house that
eats repairs. The model gives a band rather than a number because the right point
depends on the household, and hinkwise turns the household's declared risk tolerance
into a point inside the band (0 % cautious, 100 % bold, clamped so a slider bug
cannot leave the band). On top of whatever the band resolves to, money already
committed to a large purchase within three years is added: a down payment, a car, a
renovation.

**"As small as possible" is not "empty".** In the accumulating phases the model
expects the preserve bucket to fill itself: amortisation on the home and the
accrual of income pension are saving into it whether or not the household does
anything. Hinkwise therefore gives preserve no target in those phases. A bucket
with no target has no band to miss, and no rule complains about it.

**"As large as possible" is deliberately open-ended.** Growth has no upper bound in
the accumulating phases. Its ceiling is the household's own goal, and the goal is
expressed as a multiple of annual expenses (see below), which is what the "financially
independent" phase finally answers.

**The 5 + 25 rule.** In the preserving phases the buffer holds up to five annual
expenses (3 ± 2) and preserve holds 25, so together they cover thirty years of
spending. Thirty years is not arbitrary: it is the horizon the 4 % rule was tested
over. The buffer is the part that gets spent first; preserve is the part that keeps
producing the income.

**Play keeps its cap in both tables.** Play stops being something to fill when the
household stops earning, but a ceiling is not a target, so the 10 % limit on growth
survives into the preserving phases. Without it, a play bucket that happened to do
well would quietly become a large part of the wealth at exactly the point the
household can least afford to lose it.

## Money flows the other way in preserving phases

The single most visible difference between the phases is the direction money moves,
which the code names with `FlowDirection`. Accumulating phases fill the buckets;
preserving phases draw them down.

In the accumulating phases the order is fixed: pay yourself first (the model says
10–20 % of income, counting amortisation), put the next free krona in the buffer,
pay down expensive debt, then fill the rest. A buffer above its band is not a
virtue; hinkwise says so, and suggests moving the excess to growth. That advice is
only given while the buckets are being filled.

In the preserving phases the same buckets do a different job. The salary is gone or
reduced, and the buffer is the account that covers the shortfall between pension and
spending, plus the one-off costs of the next three years. When it runs low it is
refilled from preserve value; preserve in turn is refilled from growth while growth
is up. So the flow runs buffer, then preserve, then growth, in that order.

**You do not sell growth into a downturn.** The buffer holds one to five years of
spending precisely so that a bad market never forces a sale. A fall in equities is
weathered by spending the buffer, and preserve is replenished from growth when
growth is up, not when it is down. This is why the preserving buffer is measured in
years while the accumulating buffer is measured in months: it is not a cushion any
more, it is the spending account for a multi-year storm.

None of this is cosmetic. Changing the phase changes the sizing table, the direction
of flow and the headline number on the dashboard, which is why phase is a fact the
household declares rather than a display preference.

## Annual expenses as the unit of account

Kronor do not compare across households. Annual expenses do. "One year of expenses"
means the same thing to someone earning 300 000 SEK and to someone earning
3 000 000 SEK, and every stated target in the model is a multiple of it.

Hinkwise derives annual expenses as twelve times the monthly cost of needs and
wants. Saving is not spending and is not counted. From there:

- years of freedom is the capital held in the four buckets divided by annual
  expenses, so 1.0 means the capital covers one year of the life the household
  actually lives;
- the financially independent target is annual expenses times a multiplier, and
  progress toward it is the same capital divided by that target;
- both preserving-phase buffer and preserve targets are multiples of annual
  expenses, not of income.

The model's shorthand for financial independence is that the portfolio produces one
annual expense of return per year. That is where the 4 % rule enters.

### The 4 % rule and the x21 / x25 / x33 band

The 4 % rule says a portfolio can pay out about 4 % of its value per year, adjusted
for inflation, for thirty years, and historically has done so in roughly 90 % of
periods. Read backwards, the capital needed is annual expenses times 25.

Hinkwise stores the multiplier band, the withdrawal rate and a tax drag in
`Jurisdiction` (`src/domain/jurisdiction.cpp`):

| Multiplier | Withdrawal rate it implies | Where it comes from |
|---|---|---|
| x21 | 4.7 % | Bengen's 2025 revision of his own rule |
| x25 | 4 % | the classic rule |
| x33 | 3 % | 4 % minus about 1 % of annual capital tax, the Swedish ISK drag |

The default jurisdiction is Sweden, because the model is Swedish: 4 % before tax,
1 % capital tax drag, and the band 21 / 25 / 33. Every value is editable, and the
arithmetic does not depend on the jurisdiction, only the constants do.

The band is deliberately wider than the disagreement in the research. The published
estimates run from about 4.7 % down to about 3.7 %, which is a band of roughly x21 to
x27; hinkwise plans from x21 to x33. The reason is that tax drag and the household's
own tolerance for risk move the answer more than the research argument does. A
household that wants to sleep well should multiply by 33 rather than split the
difference between two papers.

The practical implication is the model's own: do not over-fit the number. Being
flexible about withdrawals in good and bad years matters more than choosing 21, 25
or 33.

## Why housing and pension are counted

For a typical Swedish household, the home and the pension entitlements are most of
the wealth. Funds and accounts are the small part. Measured against financial assets
alone, the growth bucket can look like most of the household's fortune and an equity
crash like a catastrophe. Add the home and the income pension, and the part of growth
the household actually holds in funds shrinks to a small share of the whole. A 50 %
fall in it then moves net worth by a few per cent, because the assets that dominate
the household's wealth barely move with the stock market.

That reframing is the model's central reassurance, and it cuts both ways. A
household with a home and a decent pension can usually carry more equity risk than
it feels it can, because the risk is spread over wealth the crash does not touch.
A household without those assets is carrying more risk than the same percentage
suggests.

Hinkwise counts them as first-class assets. `AssetKind` distinguishes real estate,
income pension, occupational pension, premium pension and traditional life
insurance, and each has a default bucket: the home, income pension and traditional
life insurance default to preserve value; occupational and premium pension default
to growth. `BalanceSheet` sums every holding into total assets and net worth.

Two figures come out of that, and both are needed:

- **Net worth including housing and pension** is the risk view. A fall in the growth
  bucket is measured against it, which is what `Metrics::crashImpact` does: half the
  growth bucket as a share of net worth. One of the rules raises advice when that
  share exceeds a quarter.
- **Headline net worth excluding them** is the money the household can actually act
  on this year, minus every debt. Housing and pension promises cannot be spent on
  groceries, so leaving them in would flatter the figure. It can be negative while
  total net worth is comfortably positive, which is honest: the debts are real, the
  home is not liquid.

## How hinkwise maps this to code

| Model concept | Type | Where |
|---|---|---|
| The four buckets, their order, purpose, horizon, returns, risk bands | `Bucket`, `BucketFacts`, `factsOf` | `src/domain/bucket.h`, `bucket.cpp` |
| Where the household is, and what follows from it | `Phase`, `PhasePolicy`, `policyFor` | `src/domain/phase.h`, `phase.cpp` |
| The two sizing tables, stated symbolically | `BucketTargets`, `BucketTarget`, `CapRule` | `src/domain/targets.h` |
| Those tables as money for one household | `ResolvedTargets`, `resolve`, `TargetInputs` | `src/domain/targets.h`, `targets.cpp` |
| Assets, debts, totals and per-bucket balances | `BalanceSheet`, `Holding`, `Debt` | `src/domain/balance_sheet.h`, `balance_sheet.cpp` |
| The derived ratios a screen shows | `Metrics`, `computeMetrics` | `src/domain/metrics.h`, `metrics.cpp` |
| The checks the model would make | `Rule`, `evaluateRules`, `Insight` | `src/domain/rules.h`, `rules.cpp` |

A few notes on the seams:

- Targets are stored as rules, not amounts: "0.5–6 months of expenses" is true for
  every household, "41 000 SEK" is true for one. `resolve` is the only place where
  expenses become money.
- A bucket's target and its cap are separate fields. That is what lets play have no
  target and still carry a ceiling in every phase. The cap is 10 % of the growth
  balance supplied in `TargetInputs`; excluding pension savings is a property of the
  balance handed in, not of the rule.
- The rule set is twelve independent `Rule` objects, each returning at most one
  `Insight`, sorted most urgent first. Rules that read a band ignore buckets with no
  target, which is why preserve stays quiet in the accumulating phases.
- The bucket facts and both sizing tables are transcribed from the model, and the
  tests assert them. If those numbers drift, the application stops being an
  implementation of the four-bucket model and becomes a budget tool with four
  labels.

## Attribution and disclaimer

The four-bucket model was popularised in Swedish by
[RikaTillsammans](https://rikatillsammans.se/) and its co-founder Jan Bolmeson. The
reference description this document follows is the
[RikaTillsammans article on fyra-hinkar-modellen](https://rikatillsammans.se/spara-och-investera/fyra-hinkar-modellen/).
The article states the model's guiding principle as *"har du ett starkt försvar kan
du spela ett offensivt anfall"*, which we would translate as: if you have a strong
defence, you can play an offensive attack.

The model itself is older than its Swedish name. It descends from the two-bucket
model introduced by the financial adviser Harold Evensky in the 1970s and expanded
to three buckets by Christine Benz at Morningstar; the fourth, play, was added by
RikaTillsammans. The numbers in `BucketFacts` and in the two sizing tables are
transcribed from that description. The wording in this document is hinkwise's own.

Hinkwise is an independent project. It is not affiliated with, endorsed by, sponsored
by or reviewed by RikaTillsammans, Jan Bolmeson, Morningstar, Christine Benz or
Harold Evensky, and any errors in the implementation are hinkwise's alone.

Nothing here is financial, tax or investment advice. The model is a planning aid,
not a recommendation, and the returns in the tables are long-run assumptions rather
than predictions. The default tax constants describe Sweden and are editable;
whether they match your situation is for you to check, with professional advice if
you need it.
