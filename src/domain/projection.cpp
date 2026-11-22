// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "projection.h"

#include "bucket.h"

#include <cstddef>
#include <cstdint>

namespace hinkwise::domain {

namespace {

/// Basis points in a whole: 100 % of a target, or one year of expenses.
constexpr std::int64_t basisPointsInWhole = 10000;

/// numerator / denominator in basis points, rounded half away from zero.
///
/// Kept local because the domain has no shared ratio header yet, but identical
/// to ratioOf in metrics.cpp on purpose: two ways of dividing by expenses in one
/// program would eventually disagree on a rounded basis point, and neither
/// answer would be obviously wrong. A denominator that is zero or negative has
/// no meaningful ratio, so the answer is zero rather than a division by it.
Percentage ratioOf(const Money& numerator, const Money& denominator)
{
    const std::int64_t divisor = denominator.minorUnits();
    if (divisor <= 0)
        return Percentage::fromBasisPoints(0);

    const std::int64_t product = numerator.minorUnits() * basisPointsInWhole;
    const std::int64_t adjusted = product >= 0 ? product + divisor / 2 : product - divisor / 2;
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(adjusted / divisor));
}

Money sumOf(const std::array<Money, 4>& balances)
{
    Money total = balances.front();
    for (std::size_t index = 1; index < balances.size(); ++index)
        total += balances[index];
    return total;
}

/// Fills the derived figures of a row: the total and the two ratios the model
/// headlines. Called for every row, so today is measured the same way as any
/// projected year.
void fillDerived(ProjectionYear& year, const ProjectionInput& input)
{
    year.total = sumOf(year.perBucket);
    year.yearsOfFreedom = ratioOf(year.total, input.annualExpenses);
    year.fiProgress = ratioOf(year.total, input.fiTarget);
}

} // namespace

std::vector<ProjectionYear> project(const ProjectionInput& input)
{
    // A negative horizon is a caller bug, not a household state. Treating it as
    // "today only" keeps every caller safe without inventing an exception for
    // what is otherwise a pure function.
    const int steps = input.years < 0 ? 0 : input.years;

    std::vector<ProjectionYear> years;
    years.reserve(static_cast<std::size_t>(steps) + 1);

    ProjectionYear today;
    today.year = 0;
    today.perBucket = input.startingBalances;
    fillDerived(today, input);
    years.push_back(today);

    for (int step = 1; step <= steps; ++step) {
        ProjectionYear year;
        year.year = step;

        for (const Bucket bucket : allBuckets()) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            const Money previous = years.back().perBucket[index];
            const Money growth = previous.scaledBy(input.expectedRealReturn[index]);
            year.perBucket[index] = previous + growth + input.annualContributions[index];
        }

        fillDerived(year, input);
        years.push_back(year);
    }

    return years;
}

std::optional<int> yearsToTarget(const ProjectionInput& input)
{
    // Scanning the projection instead of growing a second time keeps the two
    // answers in step: there is one place where a year is computed, so the
    // year count can never disagree with the chart beside it.
    for (const ProjectionYear& year : project(input)) {
        if (!(year.total < input.fiTarget))
            return year.year;
    }
    return std::nullopt;
}

} // namespace hinkwise::domain
