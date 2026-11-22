# 0003. Phase is the single declared fact, and a policy derives the rest

## Status

Accepted.

## Context

The model behaves differently depending on where a household is on its economic
journey. In the accumulating phases the buckets are filled, the buffer holds months
of expenses and growth has no ceiling. In the preserving phases the buffer is drawn
down and refilled from preserve, the buffer holds years of expenses, preserve holds
25–35 of them, and growth and play stop being targets. The headline number changes
too: years of freedom while building, sustainable withdrawal once preserving.

Something has to hold that difference. Three designs were available:

- **Let the household declare the targets.** Every bucket gets a number the user
  chooses. This is a generic budget tool with four labels: the advice disappears, and
  the app can no longer say anything is wrong.
- **Make phase a cosmetic label.** The screens change colour and wording but the
  sizing stays the same. The preserving phase then keeps telling the household to
  fill growth, which is the opposite of what it should say.
- **Scatter the phase checks.** Each use site asks "is this preserving?" for itself.
  The knowledge is repeated, the rule engine and the dashboard drift apart, and there
  is no one place to read what a phase means.

## Decision

**Phase is the one fact the household declares.** `Phase` has four values:
getting started, building wealth, preserving, financially independent. It is set
from the household's situation, not inferred.

**Everything that varies by phase is derived from it by a `PhasePolicy`.** The policy
is obtained from `policyFor(Phase)` and exposes exactly three things besides its own
phase: `targets()` (which sizing table applies), `flow()` (`IntoBuckets` or
`OutOfBuckets`) and `headline()` (`YearsOfFreedom` or `SafeWithdrawalRate`). The
dashboard asks the policy and renders the answer; it never switches on `Phase` itself.

**There is one switch that decides behaviour:** `policyFor`
(`src/domain/phase.cpp`). `labelOf` also switches on `Phase`, but only to produce a
display name; it decides nothing.

**There are two target tables, not four.** Getting started and building wealth share
the accumulating table; preserving and financially independent share the preserving
table. This mirrors the model, which states two sizing tables: getting started and
building wealth differ in method, not in target, and preserving and financially
independent differ in tone, not in arithmetic. A policy per phase, but not a table per
phase.

**Flow reverses at preserving.** Both accumulating policies report `IntoBuckets`, both
preserving policies report `OutOfBuckets`. `isAccumulating(phase)` answers from
`policyFor(phase).flow()` rather than from a list of phase values, so the direction is
stated once.

**The headline follows the phase.** Years of freedom while the buckets are being
filled; safe withdrawal rate once they are being drawn down. The metrics compute both
views — years of freedom and the withdrawal-based FI targets — and the policy says
which one the dashboard leads with.

**Play keeps its 10 % cap in every phase, because a cap is not a target.**
`BucketTarget` carries its `cap` as a field independent of its `kind`, so a bucket can
have no target and still carry a ceiling. Play is optional in the preserving phases,
but the ceiling remains the only thing that stops a play bucket from growing unchecked
at the point where the household has stopped earning. Conflating target and cap would
make the cap vanish exactly when it matters.

**Rules may read the phase for phase-specific advice.** "The buffer is full, so
building wealth may describe this better" only means anything in the getting-started
phase. But any target, flow direction or headline goes through the policy, never
through a second table.

## Consequences

- Phase behaviour has one home. The tests assert each phase's table, direction and
  headline, and a change to any of them is a single, visible edit.
- The user interface cannot invent a fifth behaviour or drift from the rules, because
  it has no phase knowledge of its own.
- Adding a phase forces the three questions: which table, which direction, which
  headline. There is no way to add one and inherit a silent default, because the
  switch is exhaustive.
- A number read from a screen travels `Phase` → policy → table → resolve. That is
  three steps between the declared phase and a number, and it is the cost of the
  single source.
- Two phases share each table by construction. If they ever need to differ, the shared
  table has to split; nothing is parameterised for a per-phase variation today, and the
  code does not pretend otherwise.
- A wrong phase produces advice that is correct for the wrong table, and the domain
  cannot detect it: phase is declared, not inferred. Rules can hint at it ("the buffer
  is full"), but the household decides.
