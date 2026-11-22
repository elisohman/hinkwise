// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "targets.h"

namespace hinkwise::domain {

namespace {

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

ResolvedTargets resolve(const BucketTargets& targets, const TargetInputs& inputs)
{
    const Money zero = Money::zero(inputs.monthlyExpenses.currency());
    ResolvedTargets resolved{};

    for (const Bucket bucket : allBuckets()) {
        const BucketTarget& target = targets[bucket];
        ResolvedTarget& out = resolved[static_cast<std::size_t>(bucket)];
        out.kind = target.kind;

        switch (target.kind) {
        case BucketTarget::Kind::MultipleOfExpenses: {
            out.band.low = amountFor(target, inputs, target.multiple.low);
            out.band.high = amountFor(target, inputs, target.multiple.high);
            out.point = interpolate(out.band, inputs.riskTolerance);
            break;
        }
        case BucketTarget::Kind::CapOfBucket: {
            const Money ceiling =
                balanceOf(target.capBucket, inputs).scaledBy(target.multiple.high);
            out.band.low = zero;
            out.band.high = ceiling < zero ? zero : ceiling;
            // A cap is a ceiling to stay under, not a level to reach.
            out.point = out.band.high;
            break;
        }
        case BucketTarget::Kind::AsLargeAsPossible:
        case BucketTarget::Kind::NoTarget: {
            out.band.low = zero;
            out.band.high = zero;
            out.point = zero;
            break;
        }
        }
    }

    return resolved;
}

} // namespace hinkwise::domain
