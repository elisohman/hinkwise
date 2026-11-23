// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dashboard.h"

#include "domain/jurisdiction.h"
#include "domain/phase.h"
#include "domain/rules.h"
#include "domain/targets.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace hinkwise::application {

namespace {

using domain::Bucket;
using domain::Money;

/// A valuation the household may not have recorded, or one stored in another
/// currency. Both count as zero: the sheet summarises one currency, and an
/// amount in another cannot be added to it.
Money usableOrZero(const std::optional<Money>& amount, domain::Currency currency)
{
    if (!amount.has_value() || amount->currency() != currency)
        return Money::zero(currency);
    return *amount;
}

/// buildBalanceSheet() returns zero SEK for an empty household because it has
/// no amount to read a currency from. A household can still think in EUR or
/// USD before it has entered anything, so the empty sheet is built here.
domain::BalanceSheet emptyBalanceSheet(domain::Currency currency)
{
    const Money zero = Money::zero(currency);

    domain::BalanceSheet sheet;
    sheet.totalAssets = zero;
    sheet.totalLiabilities = zero;
    sheet.netWorth = zero;
    sheet.liquidAssets = zero;
    sheet.headlineAssets = zero;
    for (const Bucket bucket : domain::allBuckets()) {
        sheet.perBucket[static_cast<std::size_t>(bucket)] = zero;
        sheet.investablePerBucket[static_cast<std::size_t>(bucket)] = zero;
    }
    return sheet;
}

/// Free money this month: what is left of income after needs, wants and
/// saving. Negative months report zero, because the plan says where next
/// month's surplus goes, not how to cover a deficit.
Money surplusOf(const domain::MonthFlow& flow)
{
    const Money surplus = flow.income - flow.needs - flow.wants - flow.saving;
    return surplus.isPositive() ? surplus : Money::zero(flow.income.currency());
}

/// The market value of every RealEstate holding, regardless of bucket.
Money realEstateValueOf(std::span<const domain::Holding> holdings, domain::Currency currency)
{
    Money total = Money::zero(currency);
    for (const domain::Holding& holding : holdings) {
        if (holding.asset != nullptr && holding.asset->kind == domain::AssetKind::RealEstate)
            total += holding.balance;
    }
    return total;
}

/// Every Mortgage liability added up.
Money mortgageTotalOf(std::span<const domain::Debt> debts, domain::Currency currency)
{
    Money total = Money::zero(currency);
    for (const domain::Debt& debt : debts) {
        if (debt.liability != nullptr && debt.liability->kind == domain::LiabilityKind::Mortgage)
            total += debt.balance;
    }
    return total;
}

} // namespace

Money totalBadLoans(std::span<const domain::Debt> debts)
{
    return domain::badLoanTotal(debts, kMortgageBenchmark);
}

DashboardSnapshot buildDashboard(IProfileRepository& profiles, IAssetRepository& assets,
                                 ILiabilityRepository& liabilities,
                                 IDebtBalanceRepository& debtBalances,
                                 IValuationRepository& valuations, IMonthFlowRepository& flows,
                                 domain::YearMonth forMonth)
{
    DashboardSnapshot snapshot;

    const Result<std::optional<domain::Profile>> loadedProfile = profiles.load();
    if (loadedProfile.ok() && loadedProfile.value().has_value()) {
        snapshot.profile = *loadedProfile.value();
        snapshot.hasProfile = true;
    }

    const domain::Currency currency = snapshot.profile.baseCurrency;
    const Money zero = Money::zero(currency);

    // The flow is optional. Without it the household has no expenses, every
    // target resolves to zero and the rules say what is missing.
    snapshot.facts.flow = domain::MonthFlow{zero, zero, zero, zero};
    const Result<std::vector<MonthFlowRecord>> recentFlows = flows.recent(1);
    if (recentFlows.ok() && !recentFlows.value().empty() &&
        !(forMonth < recentFlows.value().front().month)) {
        const domain::MonthFlow& latest = recentFlows.value().front().flow;
        if (latest.income.currency() == currency) {
            snapshot.facts.flow = latest;
            snapshot.hasFlow = true;
        }
    }

    // The results stay alive to the end of the function because Holding and
    // Debt point at the assets and liabilities inside them.
    const Result<std::vector<domain::Asset>> loadedAssets = assets.all();
    const Result<std::vector<domain::Liability>> loadedLiabilities = liabilities.all();

    std::vector<domain::Holding> holdings;
    if (loadedAssets.ok()) {
        holdings.reserve(loadedAssets.value().size());
        for (const domain::Asset& asset : loadedAssets.value()) {
            Money balance = zero;
            const Result<std::optional<Money>> latest = valuations.latest(asset.id);
            if (latest.ok())
                balance = usableOrZero(latest.value(), currency);
            holdings.push_back(domain::Holding{&asset, balance});
        }
    }

    std::vector<domain::Debt> debts;
    if (loadedLiabilities.ok()) {
        debts.reserve(loadedLiabilities.value().size());
        for (const domain::Liability& liability : loadedLiabilities.value()) {
            Money balance = zero;
            const Result<std::optional<Money>> latest = debtBalances.latest(liability.id);
            if (latest.ok())
                balance = usableOrZero(latest.value(), currency);
            debts.push_back(domain::Debt{&liability, balance});
        }
    }

    snapshot.facts.sheet = holdings.empty() && debts.empty()
                               ? emptyBalanceSheet(currency)
                               : domain::buildBalanceSheet(holdings, debts);
    snapshot.facts.realEstateValue = realEstateValueOf(holdings, currency);
    snapshot.facts.mortgageTotal = mortgageTotalOf(debts, currency);

    const domain::BucketTargets& targets = domain::policyFor(snapshot.profile.phase).targets();
    domain::TargetInputs targetInputs{currency};
    targetInputs.monthlyExpenses = snapshot.facts.flow.needs + snapshot.facts.flow.wants;
    targetInputs.annualExpenses =
        targetInputs.monthlyExpenses.scaledBy(domain::Percentage::fromBasisPoints(120000));
    targetInputs.growthBalance =
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(Bucket::Growth)];
    targetInputs.riskTolerance = snapshot.profile.riskTolerance;
    snapshot.facts.targets = domain::resolve(targets, targetInputs);

    snapshot.facts.phase = snapshot.profile.phase;
    snapshot.facts.jurisdiction = snapshot.profile.jurisdiction.id.empty()
                                      ? domain::swedishJurisdiction()
                                      : snapshot.profile.jurisdiction;
    snapshot.facts.badLoanTotal = debts.empty() ? zero : totalBadLoans(debts);

    snapshot.metrics = domain::computeMetrics(snapshot.facts);
    snapshot.insights = domain::evaluateRules(snapshot.facts, snapshot.metrics);

    domain::RebalanceInput rebalanceInput;
    rebalanceInput.phase = snapshot.profile.phase;
    rebalanceInput.balances = snapshot.facts.sheet.perBucket;
    rebalanceInput.targets = snapshot.facts.targets;
    rebalanceInput.monthlySurplus = surplusOf(snapshot.facts.flow);
    snapshot.rebalance = domain::planRebalance(rebalanceInput);

    return snapshot;
}

} // namespace hinkwise::application
