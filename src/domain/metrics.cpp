// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metrics.h"

#include <cstddef>
#include <cstdint>

namespace hinkwise::domain {

namespace {

/// Basis points in a whole: 100 % of a share, one month of expenses, or one
/// year of expenses, depending on which ratio is being computed.
constexpr std::int64_t basisPointsInWhole = 10000;

/// numerator / denominator in basis points, rounded half away from zero.
///
/// A denominator that is zero or negative has no meaningful ratio - dividing
/// by it would claim the household has infinite months of buffer, or turn a
/// negative net worth into a small crash impact - so the answer is zero.
Percentage ratioOf(const Money& numerator, const Money& denominator)
{
    const std::int64_t divisor = denominator.minorUnits();
    if (divisor <= 0)
        return Percentage::fromBasisPoints(0);

    const std::int64_t product = numerator.minorUnits() * basisPointsInWhole;
    const std::int64_t adjusted = product >= 0 ? product + divisor / 2 : product - divisor / 2;
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(adjusted / divisor));
}

Money balanceOf(const BalanceSheet& sheet, Bucket bucket)
{
    return sheet.perBucket[static_cast<std::size_t>(bucket)];
}

/// A multiplier from the jurisdiction expressed as a percentage, so a money
/// amount can be scaled by it: x25 becomes 250000 bp.
Percentage asPercentage(int multiplier)
{
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(multiplier) * 10000);
}

} // namespace

Metrics computeMetrics(const HouseholdFacts& facts)
{
    const MonthFlow& flow = facts.flow;
    const BalanceSheet& sheet = facts.sheet;

    Metrics metrics;

    metrics.monthlyExpenses = flow.needs + flow.wants;
    metrics.annualExpenses = metrics.monthlyExpenses.scaledBy(Percentage::fromBasisPoints(120000));

    // The 50/30/20 split and the savings rate read the same saving figure from
    // two angles: a share of income either way, so the two numbers agree.
    metrics.needsShare = ratioOf(flow.needs, flow.income);
    metrics.wantsShare = ratioOf(flow.wants, flow.income);
    metrics.savingShare = ratioOf(flow.saving, flow.income);
    metrics.savingsRate = ratioOf(flow.saving, flow.income);

    metrics.bufferMonths = ratioOf(balanceOf(sheet, Bucket::Buffer), metrics.monthlyExpenses);

    // Only a bucket the model actually sizes has a point to cover; an optional
    // bucket has no target figure to compare against.
    const ResolvedTarget& bufferTarget = facts.targets[static_cast<std::size_t>(Bucket::Buffer)];
    if (bufferTarget.hasTarget())
        metrics.bufferCoverage = ratioOf(balanceOf(sheet, Bucket::Buffer), bufferTarget.point);

    for (const Bucket bucket : allBuckets()) {
        metrics.bucketShare[static_cast<std::size_t>(bucket)] =
            ratioOf(balanceOf(sheet, bucket), sheet.totalAssets);
    }

    metrics.financialAssets = sheet.perBucket.front();
    for (std::size_t index = 1; index < sheet.perBucket.size(); ++index)
        metrics.financialAssets += sheet.perBucket[index];

    metrics.yearsOfFreedom = ratioOf(metrics.financialAssets, metrics.annualExpenses);

    metrics.fiTargetLow =
        metrics.annualExpenses.scaledBy(asPercentage(facts.jurisdiction.fiMultiplierLow));
    metrics.fiTargetDefault =
        metrics.annualExpenses.scaledBy(asPercentage(facts.jurisdiction.fiMultiplierDefault));
    metrics.fiTargetHigh =
        metrics.annualExpenses.scaledBy(asPercentage(facts.jurisdiction.fiMultiplierHigh));
    metrics.fiProgress = ratioOf(metrics.financialAssets, metrics.fiTargetDefault);

    // A 50 % fall in the growth bucket, as a share of net worth. Net worth
    // pins the ratio to something the household owns; below zero it says more
    // about the debts than about the risk, so the answer is zero there.
    const Money halvedGrowth =
        balanceOf(sheet, Bucket::Growth).scaledBy(Percentage::fromBasisPoints(5000));
    metrics.crashImpact = ratioOf(halvedGrowth, sheet.netWorth);

    metrics.pensionAndHousingShare =
        ratioOf(sheet.totalAssets - metrics.financialAssets, sheet.totalAssets);

    metrics.playShareOfGrowth =
        ratioOf(balanceOf(sheet, Bucket::Play), balanceOf(sheet, Bucket::Growth));

    return metrics;
}

} // namespace hinkwise::domain
