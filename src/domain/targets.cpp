// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "targets.h"

namespace hinkwise::domain {

namespace {

constexpr Percentage kMinTolerance = Percentage::fromBasisPoints(0);
constexpr Percentage kMaxTolerance = Percentage::fromBasisPoints(10000);

Money amountFor(const BucketTarget& target, const TargetInputs& inputs, Percentage multiple)
{
    const Money base =
        target.basis == ExpenseBasis::Monthly ? inputs.monthlyExpenses : inputs.annualExpenses;

    Money amount = base.scaledBy(multiple);
    if (target.includesLumpSums)
        amount += inputs.lumpSumsNextThreeYears;
    return amount;
}

Money balanceOf(Bucket bucket, const TargetInputs& inputs)
{
    // Only Growth is needed as a cap source today; anything else caps at zero
    // rather than inventing a number.
    if (bucket == Bucket::Growth)
        return inputs.growthBalance;
    return Money::zero(inputs.monthlyExpenses.currency());
}

} // namespace

TargetInputs::TargetInputs(Currency currency)
    : monthlyExpenses(Money::zero(currency))
    , annualExpenses(Money::zero(currency))
    , lumpSumsNextThreeYears(Money::zero(currency))
    , growthBalance(Money::zero(currency))
{
}

ResolvedTargets resolve(const BucketTargets& targets, const TargetInputs& inputs)
{
    const Currency currency = inputs.monthlyExpenses.currency();

    // Validate up front. Relying on the first arithmetic to notice a currency
    // mix makes the failure depend on which buckets the phase happens to read,
    // which is both phase-dependent and hard to explain.
    if (inputs.annualExpenses.currency() != currency ||
        inputs.lumpSumsNextThreeYears.currency() != currency ||
        inputs.growthBalance.currency() != currency)
        throw std::invalid_argument{"resolve: target inputs must share one currency"};

    const Money zero = Money::zero(currency);
    const Percentage tolerance = clamp(inputs.riskTolerance, kMinTolerance, kMaxTolerance);
    ResolvedTargets resolved{};

    for (const Bucket bucket : allBuckets()) {
        const BucketTarget& target = targets[bucket];
        ResolvedTarget& out = resolved[static_cast<std::size_t>(bucket)];
        out.kind = target.kind;

        if (target.kind == BucketTarget::Kind::MultipleOfExpenses) {
            out.band.low = amountFor(target, inputs, target.multiple.low);
            out.band.high = amountFor(target, inputs, target.multiple.high);
            out.point = interpolate(out.band, tolerance);
        } else {
            out.band.low = zero;
            out.band.high = zero;
            out.point = zero;
        }

        if (target.cap.has_value()) {
            const Money ceiling = balanceOf(target.cap->bucket, inputs).scaledBy(target.cap->share);
            out.cap = ceiling < zero ? zero : ceiling;
        }
    }

    return resolved;
}

} // namespace hinkwise::domain
