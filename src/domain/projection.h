// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "money.h"
#include "percentage.h"

#include <array>
#include <optional>
#include <vector>

namespace hinkwise::domain {

/// What a projection needs: the four buckets as they stand today, what is
/// added to them each year, and the two yardsticks the model measures progress
/// against.
///
/// Every return here is REAL. There is no inflation figure to supply and none
/// is applied, so a projection speaks in today's money throughout and the one
/// thing a household cannot predict stays out of the arithmetic.
struct ProjectionInput {
    /// Indexed by Bucket.
    std::array<Money, 4> startingBalances{};
    /// Indexed by Bucket. Added once per year, after that year's growth.
    std::array<Money, 4> annualContributions{};
    /// Indexed by Bucket, per year. Negative values are legal and expected:
    /// the model's band for the buffer includes a -1 % real return.
    std::array<Percentage, 4> expectedRealReturn{};
    /// For years of freedom.
    Money annualExpenses{};
    /// The FI target to measure progress against. May be zero, which means the
    /// household states no target and progress against it is zero.
    Money fiTarget{};
    /// Number of yearly steps. A negative value is treated as zero.
    int years = 30;
};

/// One row of the projection. Year 0 is today: the starting balances, untouched.
struct ProjectionYear {
    /// 0 is today, 1 is one year from now.
    int year = 0;
    /// Indexed by Bucket.
    std::array<Money, 4> perBucket{};
    Money total{};
    /// Total over annual expenses; 10 000 bp == one year of freedom.
    Percentage yearsOfFreedom{};
    /// Total over fiTarget; 10 000 bp == fully funded.
    Percentage fiProgress{};
};

/// Yearly steps from today, inclusive of today: `years + 1` rows, never fewer
/// than one.
std::vector<ProjectionYear> project(const ProjectionInput& input);

/// The first year in which the total reaches fiTarget, or nullopt when it never
/// does within the horizon. Zero when the household is already funded today,
/// which is also the answer for a zero target: a target of nothing costs
/// nothing to reach.
std::optional<int> yearsToTarget(const ProjectionInput& input);

} // namespace hinkwise::domain
