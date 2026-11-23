// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bucket.h"
#include "household.h"
#include "money.h"
#include "percentage.h"

#include <array>

namespace hinkwise::domain {

/// The numbers a screen can show, derived once from HouseholdFacts.
///
/// Percentages are reused as general ratios because the domain has no float.
/// Two of them carry a documented unit:
///   bufferMonths   10000 bp == one month of expenses
///   yearsOfFreedom 10000 bp == one year of expenses
/// Everything else is a plain share, 10000 bp == 100 %.
///
/// Zero denominators yield zero rather than infinity; the rules are what
/// complain about missing data, not the arithmetic.
struct Metrics {
    Money monthlyExpenses{};
    Money annualExpenses{};

    /// Monthly income split, the model's 50/30/20.
    Percentage needsShare{};
    Percentage wantsShare{};
    Percentage savingShare{};
    Percentage savingsRate{};

    /// Where the month's saving went, each as a share of that saving. All three
    /// are zero when the month records no breakdown, and when saving is zero
    /// there is no share to compute. savingsRate keeps its own meaning: total
    /// saving over income, amortisation included.
    Percentage bufferShareOfSaving{};
    Percentage investingShareOfSaving{};
    Percentage amortisationShareOfSaving{};

    Percentage bufferMonths{};
    /// Buffer balance divided by the buffer target's chosen point.
    Percentage bufferCoverage{};

    std::array<Percentage, 4> bucketShare{};

    /// Capital the household can act on: everything except housing and the
    /// pension kinds, i.e. BalanceSheet::headlineAssets. Not the sum of the
    /// buckets, because a home and a pension promise sit in buckets too.
    Money financialAssets{};

    Percentage yearsOfFreedom{};
    /// Progress toward fiTargetDefault, 10000 bp == fully funded.
    Percentage fiProgress{};
    Money fiTargetLow{};
    Money fiTargetDefault{};
    Money fiTargetHigh{};

    /// What a 50 % fall in the growth bucket would do to total net worth.
    /// The model's reassurance: housing and pension usually make this small.
    Percentage crashImpact{};
    Percentage pensionAndHousingShare{};
    Percentage playShareOfGrowth{};
};

Metrics computeMetrics(const HouseholdFacts& facts);

} // namespace hinkwise::domain
