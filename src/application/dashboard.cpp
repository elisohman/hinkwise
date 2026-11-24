// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dashboard.h"

#include "domain/jurisdiction.h"
#include "domain/phase.h"
#include "domain/projection.h"
#include "domain/rules.h"
#include "domain/targets.h"

#include <algorithm>
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

/// Whether a loan's recorded payment and the payment its term implies are far
/// enough apart to be worth saying so: more than five per cent of the recorded
/// one, so a rounding difference or a payment the household rounded to whole
/// kronor stays quiet. Either side missing is not a disagreement - there is
/// nothing to compare against.
bool paymentDisagrees(const Money& computed, const Money& recorded)
{
    if (!computed.isPositive() || !recorded.isPositive())
        return false;

    const std::int64_t difference = computed.minorUnits() >= recorded.minorUnits()
                                        ? computed.minorUnits() - recorded.minorUnits()
                                        : recorded.minorUnits() - computed.minorUnits();
    return difference * 100 > recorded.minorUnits() * 5;
}

/// The later of two recorded months, ignoring whichever of them is absent.
std::optional<domain::YearMonth> laterOf(std::optional<domain::YearMonth> first,
                                         std::optional<domain::YearMonth> second)
{
    if (!first)
        return second;
    if (!second)
        return first;
    return *second < *first ? first : second;
}

/// Shared by both modes: the pair is only as good as its older half, and
/// "nothing recorded" outranks every age.
InputFreshness freshnessWithin(std::optional<int> monthsSinceBalance,
                               std::optional<int> monthsSinceFlow, int freshWithin,
                               int ageingWithin)
{
    if (!monthsSinceBalance || !monthsSinceFlow)
        return InputFreshness::Missing;

    const int oldest = std::max(*monthsSinceBalance, *monthsSinceFlow);
    if (oldest <= freshWithin)
        return InputFreshness::Fresh;
    if (oldest <= ageingWithin)
        return InputFreshness::Ageing;
    return InputFreshness::Stale;
}

} // namespace

std::optional<int> monthsSince(std::optional<domain::YearMonth> then, domain::YearMonth now)
{
    if (!then)
        return std::nullopt;
    return static_cast<int>(domain::YearMonth::monthsBetween(*then, now));
}

InputFreshness freshnessOf(domain::TrackingMode mode, std::optional<int> monthsSinceBalance,
                           std::optional<int> monthsSinceFlow)
{
    switch (mode) {
    case domain::TrackingMode::Accounting:
        return freshnessWithin(monthsSinceBalance, monthsSinceFlow, 1, 2);
    case domain::TrackingMode::Planning:
        return freshnessWithin(monthsSinceBalance, monthsSinceFlow, 6, 12);
    }
    return InputFreshness::Missing;
}

InputStatus inputStatusOf(const DashboardSnapshot& snapshot)
{
    InputStatus status;
    status.balanceMonth = snapshot.latestBalanceMonth;
    status.flowMonth = snapshot.latestFlowMonth;

    if (!status.balanceMonth && !status.flowMonth) {
        status.kind = InputStatus::Kind::Empty;
        return status;
    }
    if (!status.flowMonth) {
        status.kind = InputStatus::Kind::NoFlow;
        return status;
    }
    if (!status.balanceMonth) {
        status.kind = InputStatus::Kind::NoBalances;
        return status;
    }
    if (*status.balanceMonth != *status.flowMonth) {
        status.kind = InputStatus::Kind::TwoMonths;
        return status;
    }

    // A month recorded ahead of the snapshot is not old, so it counts as
    // current, the same way freshnessOf() reads a negative count.
    const std::optional<int> sinceFlow = snapshot.monthsSinceFlow;
    const std::optional<int> sinceBalance = snapshot.monthsSinceBalance;
    const bool current = !sinceFlow || !sinceBalance || (*sinceFlow <= 0 && *sinceBalance <= 0);
    status.kind = current ? InputStatus::Kind::BothCurrent : InputStatus::Kind::BothEarlier;
    return status;
}

Money totalBadLoans(std::span<const domain::Debt> debts)
{
    return domain::badLoanTotal(debts, kMortgageBenchmark);
}

std::vector<domain::LoanTerms> loanTermsOf(std::span<const DebtInEffect> debts)
{
    std::vector<domain::LoanTerms> loans;
    loans.reserve(debts.size());
    for (const DebtInEffect& debt : debts) {
        loans.push_back(domain::LoanTerms{debt.balance, debt.interestRate, debt.style,
                                          debt.regularPayment, debt.monthlyFee, debt.termMonths});
    }
    return loans;
}

Result<DashboardSnapshot> buildDashboard(IProfileRepository& profiles, IAssetRepository& assets,
                                         ILiabilityRepository& liabilities,
                                         IDebtBalanceRepository& debtBalances,
                                         IValuationRepository& valuations,
                                         IMonthFlowRepository& flows, domain::YearMonth forMonth)
{
    using Outcome = Result<DashboardSnapshot>;

    DashboardSnapshot snapshot;

    const Result<std::optional<domain::Profile>> loadedProfile = profiles.load();
    if (loadedProfile.ok() && loadedProfile.value().has_value()) {
        snapshot.profile = *loadedProfile.value();
        snapshot.hasProfile = true;
    }

    const domain::Currency currency = snapshot.profile.baseCurrency;
    const Money zero = Money::zero(currency);
    // A household whose loans state no term has nothing implied to compare, and
    // the amount still has to be in the profile's currency rather than SEK by
    // default.
    snapshot.computedLoanPayment = zero;
    snapshot.paymentDisagrees = false;

    // The flow is optional. Without it the household has no expenses, every
    // target resolves to zero and the rules say what is missing.
    snapshot.facts.flow = domain::MonthFlow{zero, zero, zero, zero};
    const Result<std::vector<MonthFlowRecord>> recentFlows = flows.recent(1);
    // The newest recorded month is also what tells the household how old its
    // expenses are, so a failure here is a failure of the snapshot, not a month
    // that quietly becomes "never recorded".
    if (!recentFlows.ok())
        return Outcome::err(recentFlows.error());
    if (!recentFlows.value().empty()) {
        snapshot.latestFlowMonth = recentFlows.value().front().month;
        if (!(forMonth < recentFlows.value().front().month)) {
            const domain::MonthFlow& latest = recentFlows.value().front().flow;
            if (latest.income.currency() == currency) {
                snapshot.facts.flow = latest;
                snapshot.hasFlow = true;
            }
        }
    }

    const Result<std::optional<domain::YearMonth>> latestValuationMonth =
        valuations.mostRecentMonth();
    if (!latestValuationMonth.ok())
        return Outcome::err(latestValuationMonth.error());

    const Result<std::optional<domain::YearMonth>> latestDebtBalanceMonth =
        debtBalances.mostRecentMonth();
    if (!latestDebtBalanceMonth.ok())
        return Outcome::err(latestDebtBalanceMonth.error());

    // Balances are valued one holding at a time, so "when were the inputs last
    // recorded" is the more recent of the two sides of the sheet.
    snapshot.latestBalanceMonth =
        laterOf(latestValuationMonth.value(), latestDebtBalanceMonth.value());
    snapshot.monthsSinceFlow = monthsSince(snapshot.latestFlowMonth, forMonth);
    snapshot.monthsSinceBalance = monthsSince(snapshot.latestBalanceMonth, forMonth);
    snapshot.freshness = freshnessOf(snapshot.profile.trackingMode, snapshot.monthsSinceBalance,
                                     snapshot.monthsSinceFlow);

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
        snapshot.debts.reserve(loadedLiabilities.value().size());
        for (const domain::Liability& liability : loadedLiabilities.value()) {
            Money balance = zero;
            const Result<std::optional<Money>> latest = debtBalances.latest(liability.id);
            if (latest.ok())
                balance = usableOrZero(latest.value(), currency);
            debts.push_back(domain::Debt{&liability, balance});

            // The payment amounts are amounts in the household's one base
            // currency, whatever unit the row was written in, so the plan can
            // add them to the rest of the figures without a unit mismatch.
            snapshot.debts.push_back(DebtInEffect{
                liability.id, liability.name, liability.kind, liability.interestRate,
                liability.style, Money{liability.regularPayment.minorUnits(), currency},
                Money{liability.monthlyFee.minorUnits(), currency}, liability.termMonths, balance});
            // A term is a way of stating the payment too, so a loan that has one
            // is not a loan whose terms are missing.
            const bool paymentFollowsFromTerm =
                liability.style == domain::RepaymentStyle::Annuity && liability.termMonths > 0;
            if (balance.isPositive() && !liability.regularPayment.isPositive() &&
                !paymentFollowsFromTerm)
                snapshot.hasDebtWithoutTerms = true;

            // What the loan's term implies for the year ahead, computed the same
            // way the projection computes its first year - one function, so the
            // page and the curve cannot disagree about what CSN would charge.
            // The fee is part of what CSN charges, so it belongs in the payment
            // compared with the one the household recorded.
            if (!paymentFollowsFromTerm || !balance.isPositive())
                continue;

            const Money computed =
                domain::annuityPayment(balance, liability.interestRate, liability.termMonths,
                                       Money{liability.monthlyFee.minorUnits(), currency});
            snapshot.computedLoanPayment += computed;
            if (paymentDisagrees(computed, Money{liability.regularPayment.minorUnits(), currency}))
                snapshot.paymentDisagrees = true;
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

    return Outcome::ok(std::move(snapshot));
}

} // namespace hinkwise::application
