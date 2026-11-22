// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/balance_sheet.h"
#include "domain/household.h"
#include "domain/insight.h"
#include "domain/metrics.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/profile.h"
#include "domain/rebalance.h"
#include "domain/year_month.h"
#include "ports.h"

#include <span>
#include <vector>

namespace hinkwise::application {

/// A typical Swedish mortgage rate, the model's line between cheap debt and
/// debt worth paying down first: 4.50 %. A loan priced above it is what
/// isBadLoan() calls bad.
inline constexpr domain::Percentage kMortgageBenchmark = domain::Percentage::fromBasisPoints(450);

/// Everything a dashboard screen shows, in one value.
///
/// The pieces are the same objects every calculation already speaks in, so a
/// view model formats the numbers instead of recomputing them, and two screens
/// cannot disagree about the same household.
struct DashboardSnapshot {
    domain::Profile profile;
    domain::HouseholdFacts facts;
    domain::Metrics metrics;
    std::vector<domain::Insight> insights;
    domain::RebalanceProposal rebalance;
    /// False until the household has been created. The other fields are then
    /// zero and the screen shows its empty state.
    bool hasProfile = false;
    /// False until the first month flow is recorded, which is the point at
    /// which expenses become known and the buckets can be sized.
    bool hasFlow = false;
};

/// The bad loans among `debts`, summed and priced against kMortgageBenchmark.
domain::Money totalBadLoans(std::span<const domain::Debt> debts);

/// Reads the household and computes the dashboard for one month.
///
/// `debtBalances` sits next to `liabilities` because a liability carries its
/// rate, not what is left to repay: the liability side of the sheet needs the
/// latest balance of every debt.
///
/// `forMonth` is the month the snapshot describes. A month flow recorded for a
/// later month is not part of it yet; otherwise the most recent flow supplies
/// the expenses.
///
/// Missing data degrades instead of failing: no profile means hasProfile is
/// false and every figure is zero, an asset without a valuation counts as zero,
/// and a repository that cannot read a row leaves that row out. The function
/// never throws.
DashboardSnapshot buildDashboard(IProfileRepository& profiles, IAssetRepository& assets,
                                 ILiabilityRepository& liabilities,
                                 IDebtBalanceRepository& debtBalances,
                                 IValuationRepository& valuations, IMonthFlowRepository& flows,
                                 domain::YearMonth forMonth);

} // namespace hinkwise::application
