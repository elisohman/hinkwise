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

#include <optional>
#include <span>
#include <vector>

namespace hinkwise::application {

/// A typical Swedish mortgage rate, the model's line between cheap debt and
/// debt worth paying down first: 4.50 %. A loan priced above it is what
/// isBadLoan() calls bad.
inline constexpr domain::Percentage kMortgageBenchmark = domain::Percentage::fromBasisPoints(450);

/// Whole months from `then` to `now`, or std::nullopt when nothing has ever
/// been recorded. A month recorded in the future counts as negative, which the
/// freshness policy reads as "fresh" because it is not old.
std::optional<int> monthsSince(std::optional<domain::YearMonth> then, domain::YearMonth now);

/// How much the household's inputs can be trusted to still describe it.
///
/// Missing is not a synonym for Stale: it says nothing has been recorded yet,
/// while Stale says something was recorded and is now out of date. The two ask
/// the household for different things.
enum class InputFreshness {
    Fresh,
    Ageing,
    Stale,
    Missing,
};

/// The freshness of the balances and the flow under one tracking mode.
///
/// The thresholds differ because the two modes ask different things of the
/// household:
///
///   Accounting - Fresh when both are at most 1 month old, Ageing up to 2,
///   Stale beyond 2, Missing when either has never been recorded. A missed
///   month is a hole in the record: last month's figure is the newest one there
///   is, and the entry for this month is not written yet.
///
///   Planning - Fresh when both are at most 6 months old, Ageing up to 12,
///   Stale beyond 12, Missing when either has never been recorded. The budget is
///   a standing assumption rather than a monthly entry, so an old figure is
///   still a defensible assumption - just an old one. A year is where "I set
///   this once" stops being an explanation.
///
/// Either side being absent is Missing whatever the mode: a budget with no
/// expenses recorded cannot be judged fresh, and a flow with no balances is not
/// a plan either.
InputFreshness freshnessOf(domain::TrackingMode mode, std::optional<int> monthsSinceBalance,
                           std::optional<int> monthsSinceFlow);

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

    /// When the inputs were last recorded, whichever month it was: the newest
    /// flow, and the newest of any asset valuation and any debt balance. Both
    /// are std::nullopt until something has been recorded.
    std::optional<domain::YearMonth> latestFlowMonth;
    std::optional<domain::YearMonth> latestBalanceMonth;
    /// The same two months counted back from the month the snapshot describes,
    /// ready for the screen to phrase; std::nullopt when never recorded.
    std::optional<int> monthsSinceFlow;
    std::optional<int> monthsSinceBalance;
    /// freshnessOf() of the above under the profile's tracking mode. Missing in
    /// the default-constructed snapshot, because nothing is recorded there.
    InputFreshness freshness = InputFreshness::Missing;
};

/// Which months the household's two inputs are on record for, as one value.
///
/// The kind is the whole decision - which sides exist, whether they agree on a
/// month, and whether that month is the one the screen describes - so the
/// wording that follows from it is the view model's alone, and a test can read
/// the decision without going through a screen.
///
/// A single date is only ever claimed when both sides carry it. Two different
/// months stay two months here rather than being collapsed into the later one,
/// because "balances recorded through March; cashflow for May" is the honest
/// sentence and "current" is not.
struct InputStatus {
    enum class Kind {
        /// Nothing recorded on either side yet.
        Empty,
        /// Balances exist and no month flow does, so there are no expenses to
        /// size the buckets from.
        NoFlow,
        /// A month flow exists and no balances do, so the plan has nothing to
        /// project from.
        NoBalances,
        /// Both recorded for the month the screen describes, or a later one.
        BothCurrent,
        /// Both recorded for the same earlier month.
        BothEarlier,
        /// Both recorded, for two different months.
        TwoMonths,
    };

    Kind kind = Kind::Empty;
    std::optional<domain::YearMonth> balanceMonth;
    std::optional<domain::YearMonth> flowMonth;
};

/// inputStatusOf() of a snapshot: read from the months it already carries, so
/// the screen and the sentence cannot disagree about what is on record.
InputStatus inputStatusOf(const DashboardSnapshot& snapshot);

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
///
/// The provenance reads are the exception. When an input was last recorded is
/// part of the answer - it decides whether the household is told its data is
/// old or that it has none - so a read that fails there is an error rather than
/// a date left empty, which would read as "never recorded".
Result<DashboardSnapshot> buildDashboard(IProfileRepository& profiles, IAssetRepository& assets,
                                         ILiabilityRepository& liabilities,
                                         IDebtBalanceRepository& debtBalances,
                                         IValuationRepository& valuations,
                                         IMonthFlowRepository& flows, domain::YearMonth forMonth);

} // namespace hinkwise::application
