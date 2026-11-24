// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/dashboard.h"
#include "application/ports.h"
#include "application/result.h"
#include "application/use_cases.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/household.h"
#include "domain/insight.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/projection.h"
#include "domain/year_month.h"

#include <QTest>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace hinkwise::application;
using namespace hinkwise::domain;

namespace {

constexpr YearMonth kSnapshotMonth{2026, 3};

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

std::int64_t ore(Money amount)
{
    return amount.minorUnits();
}

std::int32_t bps(Percentage percentage)
{
    return percentage.basisPoints();
}

MonthFlow flowOf(qint64 income, qint64 needs, qint64 wants, qint64 saving)
{
    return MonthFlow{sek(income), sek(needs), sek(wants), sek(saving)};
}

std::int64_t bucketOf(const BalanceSheet& sheet, Bucket bucket)
{
    return ore(sheet.perBucket[static_cast<std::size_t>(bucket)]);
}

// ---------------------------------------------------------------------------
// In-memory doubles for the ports. They keep exactly what the dashboard reads
// and nothing else, so a test can state a household as data instead of a
// database.
// ---------------------------------------------------------------------------

class FakeProfileRepository final : public IProfileRepository {
public:
    std::optional<Profile> stored;

    Result<std::optional<Profile>> load() const override
    {
        return Result<std::optional<Profile>>::ok(stored);
    }

    Status save(const Profile& profile) override
    {
        stored = profile;
        return Status::success();
    }
};

class FakeAssetRepository final : public IAssetRepository {
public:
    std::vector<Asset> stored;

    Result<std::vector<Asset>> all() const override
    {
        return Result<std::vector<Asset>>::ok(stored);
    }

    Status upsert(const Asset& asset) override
    {
        const auto existing =
            std::find_if(stored.begin(), stored.end(),
                         [&asset](const Asset& item) { return item.id == asset.id; });
        if (existing == stored.end())
            stored.push_back(asset);
        else
            *existing = asset;
        return Status::success();
    }

    Status remove(std::string_view id) override
    {
        std::erase_if(stored, [id](const Asset& asset) { return asset.id == id; });
        return Status::success();
    }
};

class FakeLiabilityRepository final : public ILiabilityRepository {
public:
    std::vector<Liability> stored;

    Result<std::vector<Liability>> all() const override
    {
        return Result<std::vector<Liability>>::ok(stored);
    }

    Status upsert(const Liability& liability) override
    {
        const auto existing =
            std::find_if(stored.begin(), stored.end(),
                         [&liability](const Liability& item) { return item.id == liability.id; });
        if (existing == stored.end())
            stored.push_back(liability);
        else
            *existing = liability;
        return Status::success();
    }

    Status remove(std::string_view id) override
    {
        std::erase_if(stored, [id](const Liability& liability) { return liability.id == id; });
        return Status::success();
    }
};

/// The later of two recorded months, ignoring whichever of them is absent. The
/// dashboard's provenance and the doubles below need the same rule.
std::optional<YearMonth> laterOf(std::optional<YearMonth> first, std::optional<YearMonth> second)
{
    if (!first)
        return second;
    if (!second)
        return first;
    return *second < *first ? first : second;
}

/// Valuations, month by month per asset: the dashboard reads the newest one and
/// the newest month anything was recorded in.
class FakeValuationRepository final : public IValuationRepository {
public:
    std::map<std::string, std::map<YearMonth, Money>> stored;
    bool failMostRecentMonth = false;

    Result<std::optional<Money>> balanceAt(std::string_view assetId, YearMonth month) const override
    {
        const auto asset = stored.find(std::string{assetId});
        if (asset == stored.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        const auto point = asset->second.find(month);
        if (point == asset->second.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(point->second);
    }

    Result<std::optional<Money>> latest(std::string_view assetId) const override
    {
        const auto asset = stored.find(std::string{assetId});
        if (asset == stored.end() || asset->second.empty())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(asset->second.rbegin()->second);
    }

    Result<std::vector<MoneyPoint>> history(std::string_view assetId) const override
    {
        std::vector<MoneyPoint> points;
        const auto asset = stored.find(std::string{assetId});
        if (asset != stored.end()) {
            for (const auto& [month, amount] : asset->second)
                points.push_back(MoneyPoint{month, amount});
        }
        return Result<std::vector<MoneyPoint>>::ok(std::move(points));
    }

    Result<std::optional<YearMonth>> mostRecentMonth() const override
    {
        if (failMostRecentMonth)
            return Result<std::optional<YearMonth>>::err("valuations could not be read");

        std::optional<YearMonth> newest;
        for (const auto& asset : stored) {
            if (!asset.second.empty())
                newest = laterOf(newest, asset.second.rbegin()->first);
        }
        return Result<std::optional<YearMonth>>::ok(newest);
    }

    Status upsert(std::string_view assetId, YearMonth month, Money balance) override
    {
        stored[std::string{assetId}][month] = balance;
        return Status::success();
    }
};

/// Debt balances, month by month per liability, the same shape as the valuations.
class FakeDebtBalanceRepository final : public IDebtBalanceRepository {
public:
    std::map<std::string, std::map<YearMonth, Money>> stored;
    bool failMostRecentMonth = false;

    Result<std::optional<Money>> balanceAt(std::string_view liabilityId,
                                           YearMonth month) const override
    {
        const auto debt = stored.find(std::string{liabilityId});
        if (debt == stored.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        const auto point = debt->second.find(month);
        if (point == debt->second.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(point->second);
    }

    Result<std::optional<Money>> latest(std::string_view liabilityId) const override
    {
        const auto debt = stored.find(std::string{liabilityId});
        if (debt == stored.end() || debt->second.empty())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(debt->second.rbegin()->second);
    }

    Result<std::vector<MoneyPoint>> history(std::string_view liabilityId) const override
    {
        std::vector<MoneyPoint> points;
        const auto debt = stored.find(std::string{liabilityId});
        if (debt != stored.end()) {
            for (const auto& [month, amount] : debt->second)
                points.push_back(MoneyPoint{month, amount});
        }
        return Result<std::vector<MoneyPoint>>::ok(std::move(points));
    }

    Result<std::optional<YearMonth>> mostRecentMonth() const override
    {
        if (failMostRecentMonth)
            return Result<std::optional<YearMonth>>::err("debt balances could not be read");

        std::optional<YearMonth> newest;
        for (const auto& debt : stored) {
            if (!debt.second.empty())
                newest = laterOf(newest, debt.second.rbegin()->first);
        }
        return Result<std::optional<YearMonth>>::ok(newest);
    }

    Status upsert(std::string_view liabilityId, YearMonth month, Money balance) override
    {
        stored[std::string{liabilityId}][month] = balance;
        return Status::success();
    }
};

class FakeMonthFlowRepository final : public IMonthFlowRepository {
public:
    std::vector<MonthFlowRecord> stored;
    bool failRecent = false;

    Result<std::optional<MonthFlow>> at(YearMonth month) const override
    {
        const auto found =
            std::find_if(stored.begin(), stored.end(),
                         [month](const MonthFlowRecord& record) { return record.month == month; });
        if (found == stored.end())
            return Result<std::optional<MonthFlow>>::ok(std::nullopt);
        return Result<std::optional<MonthFlow>>::ok(found->flow);
    }

    Result<std::vector<MonthFlowRecord>> recent(int count) const override
    {
        if (failRecent)
            return Result<std::vector<MonthFlowRecord>>::err("month flows could not be read");

        std::vector<MonthFlowRecord> newest = stored;
        std::sort(newest.begin(), newest.end(),
                  [](const MonthFlowRecord& left, const MonthFlowRecord& right) {
                      return right.month < left.month;
                  });

        if (count < 0)
            count = 0;
        if (static_cast<std::size_t>(count) < newest.size())
            newest.erase(newest.begin() + static_cast<std::ptrdiff_t>(count), newest.end());

        return Result<std::vector<MonthFlowRecord>>::ok(std::move(newest));
    }

    Status upsert(YearMonth month, const MonthFlow& flow) override
    {
        const auto existing =
            std::find_if(stored.begin(), stored.end(),
                         [month](const MonthFlowRecord& record) { return record.month == month; });
        if (existing == stored.end())
            stored.push_back(MonthFlowRecord{month, flow});
        else
            existing->flow = flow;
        return Status::success();
    }
};

struct FakeHousehold {
    FakeProfileRepository profiles;
    FakeAssetRepository assets;
    FakeLiabilityRepository liabilities;
    FakeDebtBalanceRepository debtBalances;
    FakeValuationRepository valuations;
    FakeMonthFlowRepository flows;

    /// The snapshot, or the empty household when buildDashboard() failed. Tests
    /// about a failure ask buildResult() for the message instead.
    DashboardSnapshot build(YearMonth month)
    {
        const Result<DashboardSnapshot> built = buildResult(month);
        return built.ok() ? built.value() : DashboardSnapshot{};
    }

    Result<DashboardSnapshot> buildResult(YearMonth month)
    {
        return buildDashboard(profiles, assets, liabilities, debtBalances, valuations, flows,
                              month);
    }
};

Profile sampleProfile()
{
    Profile profile;
    profile.displayName = "Elis";
    profile.baseCurrency = Currency::Sek;
    profile.phase = Phase::BuildingWealth;
    profile.jurisdiction = swedishJurisdiction();
    return profile;
}

/// 50 000 kr of income split 20 000 / 4 000 / 6 000, a 10 000 kr buffer, a
/// 400 000 kr fund, a 2 000 000 kr flat, 10 000 kr of play money, a 1 500 000 kr
/// mortgage and a 20 000 kr credit card. Expenses are therefore 24 000 kr a
/// month, or 288 000 kr a year.
void populate(FakeHousehold& household)
{
    household.profiles.stored = sampleProfile();

    household.assets.stored.push_back(
        makeAsset("buffer", "Buffer account", AssetKind::BankAccount));
    household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
    household.assets.stored.push_back(makeAsset("flat", "Flat", AssetKind::RealEstate));
    household.assets.stored.push_back(makeAsset("crypto", "Crypto", AssetKind::Crypto));

    household.liabilities.stored.push_back(makeLiability(
        "mortgage", "Mortgage", LiabilityKind::Mortgage, Percentage::fromBasisPoints(400)));
    household.liabilities.stored.push_back(makeLiability(
        "card", "Credit card", LiabilityKind::CreditCard, Percentage::fromBasisPoints(1995)));

    household.valuations.upsert("buffer", kSnapshotMonth, sek(1000000));
    household.valuations.upsert("fund", kSnapshotMonth, sek(40000000));
    household.valuations.upsert("flat", kSnapshotMonth, sek(200000000));
    household.valuations.upsert("crypto", kSnapshotMonth, sek(1000000));

    household.debtBalances.upsert("mortgage", kSnapshotMonth, sek(150000000));
    household.debtBalances.upsert("card", kSnapshotMonth, sek(2000000));

    household.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth, flowOf(5000000, 2000000, 400000, 600000)});
}

} // namespace

class TestDashboard : public QObject {
    Q_OBJECT

private slots:
    void emptyDatabaseHasNoProfileAndZeroMetrics();
    void populatedHouseholdReportsItsMetrics();
    void populatedHouseholdShowsItsInsightsAndRebalance();
    void projectionStartsFromInvestableCapital();
    void assetWithoutValuationCountsAsZero();
    void flowIsUsedWhenItIsNotLaterThanTheSnapshotMonth();
    void flowLaterThanTheSnapshotMonthIsNotPartOfIt();
    void totalBadLoansSumsOnlyDebtsAboveTheBenchmark();
    void debtsCarryTheirTermsAndLatestBalance();

    void monthsSinceCountsWholeMonths();
    void accountingFreshnessTreatsAMissedMonthAsAHole();
    void planningFreshnessLetsTheAssumptionStandForAYear();

    void provenanceReportsWhenEachInputWasLastRecorded();
    void balanceMonthIsTheLaterOfTheTwoSides();
    void emptyHouseholdHasNoProvenance();
    void theModeDecidesWhatAnOldMonthMeans();
    void aFailedProvenanceReadIsAnError();
    void backfilledMonthsAreKeptAndDoNotBecomeTheLatest();

    void inputStatusNamesAHouseholdWithNothingRecorded();
    void inputStatusKeepsTwoMonthsAsTwoMonths();
    void inputStatusOnlyCallsAMonthCurrentWhenBothSidesCarryIt();
};

void TestDashboard::emptyDatabaseHasNoProfileAndZeroMetrics()
{
    FakeHousehold household;
    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.hasProfile, false);
    QCOMPARE(snapshot.hasFlow, false);

    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), qint64(0));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), qint64(0));
    QCOMPARE(ore(snapshot.facts.sheet.totalAssets), qint64(0));
    QCOMPARE(ore(snapshot.facts.sheet.netWorth), qint64(0));
    QCOMPARE(bps(snapshot.metrics.yearsOfFreedom), 0);
    QCOMPARE(bps(snapshot.metrics.fiProgress), 0);
}

void TestDashboard::populatedHouseholdReportsItsMetrics()
{
    FakeHousehold household;
    populate(household);

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.hasProfile, true);
    QCOMPARE(snapshot.hasFlow, true);
    QCOMPARE(QString::fromStdString(snapshot.profile.displayName), QStringLiteral("Elis"));
    QCOMPARE(static_cast<int>(snapshot.profile.phase), static_cast<int>(Phase::BuildingWealth));

    // Expenses: needs and wants only, monthly and annual.
    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), qint64(2400000));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), qint64(28800000));

    // The sheet: 2 420 000 kr of assets against 1 520 000 kr of debt.
    QCOMPARE(ore(snapshot.facts.sheet.totalAssets), qint64(242000000));
    QCOMPARE(ore(snapshot.facts.sheet.totalLiabilities), qint64(152000000));
    QCOMPARE(ore(snapshot.facts.sheet.netWorth), qint64(90000000));

    // Housing is not money the household can act on: 420 000 kr is.
    QCOMPARE(ore(snapshot.metrics.financialAssets), qint64(42000000));
    std::int64_t investable = 0;
    for (const Bucket bucket : allBuckets())
        investable +=
            ore(snapshot.facts.sheet.investablePerBucket[static_cast<std::size_t>(bucket)]);
    QCOMPARE(investable, ore(snapshot.metrics.financialAssets));
    QCOMPARE(ore(snapshot.facts.sheet.headlineNetWorth(false)), qint64(42000000 - 152000000));

    QCOMPARE(bps(snapshot.metrics.needsShare), 4000);
    QCOMPARE(bps(snapshot.metrics.wantsShare), 800);
    QCOMPARE(bps(snapshot.metrics.savingsRate), 1200);
    QCOMPARE(bps(snapshot.metrics.bufferMonths), 4167);
    QCOMPARE(bps(snapshot.metrics.yearsOfFreedom), 14583);
    QCOMPARE(bps(snapshot.metrics.fiProgress), 583);
    QCOMPARE(bps(snapshot.metrics.crashImpact), 2222);
    QCOMPARE(bps(snapshot.metrics.pensionAndHousingShare), 8264);

    // The credit card is above the mortgage benchmark; the mortgage is not.
    QCOMPARE(ore(snapshot.facts.badLoanTotal), qint64(2000000));

    // Per-kind totals the property rule compares: the flat at market value and
    // the mortgage as a separate debt.
    QCOMPARE(ore(snapshot.facts.realEstateValue), qint64(200000000));
    QCOMPARE(ore(snapshot.facts.mortgageTotal), qint64(150000000));
}

void TestDashboard::populatedHouseholdShowsItsInsightsAndRebalance()
{
    FakeHousehold household;
    populate(household);

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    // The 10 000 kr buffer is below the building-phase band, and the credit
    // card is a bad loan running beside it. Both are warnings, so the second
    // sort key (the rule id) decides their order.
    const std::size_t expectedInsights = 2;
    QCOMPARE(snapshot.insights.size(), expectedInsights);
    QCOMPARE(QString::fromStdString(snapshot.insights[0].id),
             QStringLiteral("bad-loan-while-buffer-thin"));
    QCOMPARE(QString::fromStdString(snapshot.insights[1].id), QStringLiteral("buffer-underfunded"));
    QCOMPARE(static_cast<int>(snapshot.insights[0].severity), static_cast<int>(Severity::Warning));

    QVERIFY(snapshot.rebalance.nextContribution.has_value());
    QCOMPARE(static_cast<int>(*snapshot.rebalance.nextContribution),
             static_cast<int>(Bucket::Buffer));
    QVERIFY(snapshot.rebalance.transfers.empty());
}

void TestDashboard::projectionStartsFromInvestableCapital()
{
    // A household whose only asset is the flat it lives in. Its buckets hold
    // the flat, but it has no capital it can spend.
    FakeHousehold houseOnly;
    houseOnly.profiles.stored = sampleProfile();
    houseOnly.assets.stored.push_back(makeAsset("flat", "Flat", AssetKind::RealEstate));
    houseOnly.valuations.upsert("flat", kSnapshotMonth, sek(200000000));
    houseOnly.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth, flowOf(5000000, 2000000, 400000, 600000)});

    const DashboardSnapshot house = houseOnly.build(kSnapshotMonth);
    QCOMPARE(bucketOf(house.facts.sheet, Bucket::Preserve), qint64(200000000));
    QCOMPARE(ore(house.facts.sheet.headlineAssets), qint64(0));
    for (const Bucket bucket : allBuckets())
        QCOMPARE(ore(house.facts.sheet.investablePerBucket[static_cast<std::size_t>(bucket)]),
                 qint64(0));

    // The plan page seeds its projection from the investable buckets. Starting
    // from the whole sheet would call this household funded in year zero.
    ProjectionInput houseProjection;
    houseProjection.startingBalances = house.facts.sheet.investablePerBucket;
    houseProjection.annualExpenses = house.metrics.annualExpenses;
    houseProjection.fiTarget = house.metrics.fiTargetDefault;
    houseProjection.years = 30;

    QCOMPARE(bps(project(houseProjection).front().yearsOfFreedom), 0);
    QVERIFY(!yearsToTarget(houseProjection).has_value());

    // The same target held in a bank account is spendable capital, and is
    // counted: the exclusion is about housing and pensions, not about cash.
    FakeHousehold cashOnly;
    cashOnly.profiles.stored = sampleProfile();
    cashOnly.assets.stored.push_back(makeAsset("buffer", "Buffer account", AssetKind::BankAccount));
    cashOnly.valuations.upsert("buffer", kSnapshotMonth, sek(1000000000));
    cashOnly.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth, flowOf(5000000, 2000000, 400000, 600000)});

    const DashboardSnapshot cash = cashOnly.build(kSnapshotMonth);
    QCOMPARE(bucketOf(cash.facts.sheet, Bucket::Buffer), qint64(1000000000));
    QCOMPARE(ore(cash.facts.sheet.investablePerBucket[static_cast<std::size_t>(Bucket::Buffer)]),
             qint64(1000000000));
    QCOMPARE(ore(cash.facts.sheet.headlineAssets), qint64(1000000000));

    ProjectionInput cashProjection;
    cashProjection.startingBalances = cash.facts.sheet.investablePerBucket;
    cashProjection.annualExpenses = cash.metrics.annualExpenses;
    cashProjection.fiTarget = cash.metrics.fiTargetDefault;
    cashProjection.years = 30;

    QVERIFY(bps(project(cashProjection).front().yearsOfFreedom) > 0);
    const std::optional<int> reached = yearsToTarget(cashProjection);
    QVERIFY(reached.has_value());
    QCOMPARE(*reached, 0);
}

void TestDashboard::assetWithoutValuationCountsAsZero()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.assets.stored.push_back(
        makeAsset("buffer", "Buffer account", AssetKind::BankAccount));
    household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
    household.valuations.upsert("buffer", kSnapshotMonth, sek(1000000));

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.hasProfile, true);
    QCOMPARE(ore(snapshot.facts.sheet.totalAssets), qint64(1000000));
    QCOMPARE(bucketOf(snapshot.facts.sheet, Bucket::Buffer), qint64(1000000));
    QCOMPARE(bucketOf(snapshot.facts.sheet, Bucket::Growth), qint64(0));
}

void TestDashboard::flowIsUsedWhenItIsNotLaterThanTheSnapshotMonth()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth.plusMonths(-1), flowOf(4000000, 1500000, 500000, 500000)});

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.hasFlow, true);
    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), qint64(2000000));
}

void TestDashboard::flowLaterThanTheSnapshotMonthIsNotPartOfIt()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth.plusMonths(1), flowOf(4000000, 1500000, 500000, 500000)});

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.hasFlow, false);
    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), qint64(0));
}

void TestDashboard::totalBadLoansSumsOnlyDebtsAboveTheBenchmark()
{
    const Liability mortgage = makeLiability("mortgage", "Mortgage", LiabilityKind::Mortgage,
                                             Percentage::fromBasisPoints(450));
    const Liability card = makeLiability("card", "Credit card", LiabilityKind::CreditCard,
                                         Percentage::fromBasisPoints(1995));

    const std::vector<Debt> debts{{&mortgage, sek(200000000)}, {&card, sek(2000000)}};
    QCOMPARE(ore(totalBadLoans(debts)), qint64(2000000));

    const std::vector<Debt> noDebts;
    QCOMPARE(ore(totalBadLoans(noDebts)), qint64(0));
}

void TestDashboard::debtsCarryTheirTermsAndLatestBalance()
{
    FakeHousehold household;
    populate(household);
    household.liabilities.stored.push_back(makeLiability(
        "csn", "Student loan", LiabilityKind::StudentLoan, Percentage::fromBasisPoints(150),
        RepaymentStyle::Annuity, sek(150000), sek(5000)));
    household.debtBalances.upsert("csn", kSnapshotMonth, sek(30000000));

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QCOMPARE(snapshot.debts.size(), std::size_t{3});

    const auto find = [&snapshot](std::string_view id) -> const DebtInEffect& {
        for (const DebtInEffect& debt : snapshot.debts) {
            if (debt.id == id)
                return debt;
        }
        return snapshot.debts.front();
    };

    const DebtInEffect& csn = find("csn");
    QCOMPARE(static_cast<int>(csn.kind), static_cast<int>(LiabilityKind::StudentLoan));
    QCOMPARE(bps(csn.interestRate), 150);
    QCOMPARE(static_cast<int>(csn.style), static_cast<int>(RepaymentStyle::Annuity));
    QCOMPARE(ore(csn.regularPayment), qint64(150000));
    QCOMPARE(ore(csn.monthlyFee), qint64(5000));
    QCOMPARE(ore(csn.balance), qint64(30000000));

    // The mortgage has no terms recorded in the fixture, and the credit card
    // still owes 20 000 kr: that is the state the plan must not draw as paid
    // down, so the snapshot says the terms are missing.
    const DebtInEffect& card = find("card");
    QCOMPARE(ore(card.balance), qint64(2000000));
    QVERIFY(card.regularPayment.isZero());
    QVERIFY(snapshot.hasDebtWithoutTerms);

    // Recording a payment for every loan clears the warning.
    FakeHousehold paid;
    populate(paid);
    for (Liability& liability : paid.liabilities.stored)
        liability.regularPayment = sek(100000);
    QVERIFY(!paid.build(kSnapshotMonth).hasDebtWithoutTerms);
}

void TestDashboard::monthsSinceCountsWholeMonths()
{
    const YearMonth now{2026, 3};

    // Nothing recorded is not zero months old; it is nothing to measure.
    QVERIFY(!monthsSince(std::nullopt, now).has_value());
    QCOMPARE(*monthsSince(now, now), 0);
    QCOMPARE(*monthsSince(now.plusMonths(-1), now), 1);
    QCOMPARE(*monthsSince(now.plusMonths(-6), now), 6);
    QCOMPARE(*monthsSince(now.plusMonths(-13), now), 13);

    // A month recorded ahead of the snapshot is not old, so the count runs
    // negative rather than being clamped: the freshness policy reads it as
    // whatever "not old" is in that mode.
    QCOMPARE(*monthsSince(now.plusMonths(1), now), -1);
}

void TestDashboard::accountingFreshnessTreatsAMissedMonthAsAHole()
{
    const TrackingMode mode = TrackingMode::Accounting;

    // One month is the newest entry there can be: this month is not written
    // yet. Two is late, and the third month is a hole in the record.
    QCOMPARE(static_cast<int>(freshnessOf(mode, 0, 0)), static_cast<int>(InputFreshness::Fresh));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 1, 1)), static_cast<int>(InputFreshness::Fresh));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 1, 2)), static_cast<int>(InputFreshness::Ageing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 2, 2)), static_cast<int>(InputFreshness::Ageing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 3, 0)), static_cast<int>(InputFreshness::Stale));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 0, 3)), static_cast<int>(InputFreshness::Stale));

    // Either side absent is Missing, whichever side it is.
    QCOMPARE(static_cast<int>(freshnessOf(mode, std::nullopt, 0)),
             static_cast<int>(InputFreshness::Missing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 0, std::nullopt)),
             static_cast<int>(InputFreshness::Missing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, std::nullopt, std::nullopt)),
             static_cast<int>(InputFreshness::Missing));
}

void TestDashboard::planningFreshnessLetsTheAssumptionStandForAYear()
{
    const TrackingMode mode = TrackingMode::Planning;

    // Half a year is still a budget nobody had to revisit; a year is where it
    // stops being "set once" and starts being old.
    QCOMPARE(static_cast<int>(freshnessOf(mode, 0, 0)), static_cast<int>(InputFreshness::Fresh));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 6, 6)), static_cast<int>(InputFreshness::Fresh));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 7, 7)), static_cast<int>(InputFreshness::Ageing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 12, 12)), static_cast<int>(InputFreshness::Ageing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 13, 13)), static_cast<int>(InputFreshness::Stale));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 6, 13)), static_cast<int>(InputFreshness::Stale));

    QCOMPARE(static_cast<int>(freshnessOf(mode, std::nullopt, 6)),
             static_cast<int>(InputFreshness::Missing));
    QCOMPARE(static_cast<int>(freshnessOf(mode, 6, std::nullopt)),
             static_cast<int>(InputFreshness::Missing));
}

void TestDashboard::provenanceReportsWhenEachInputWasLastRecorded()
{
    FakeHousehold household;
    populate(household);

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QVERIFY(snapshot.latestFlowMonth.has_value());
    QCOMPARE(QString::fromStdString(snapshot.latestFlowMonth->toString()),
             QStringLiteral("2026-03"));
    QVERIFY(snapshot.latestBalanceMonth.has_value());
    QCOMPARE(QString::fromStdString(snapshot.latestBalanceMonth->toString()),
             QStringLiteral("2026-03"));

    QVERIFY(snapshot.monthsSinceFlow.has_value());
    QCOMPARE(*snapshot.monthsSinceFlow, 0);
    QVERIFY(snapshot.monthsSinceBalance.has_value());
    QCOMPARE(*snapshot.monthsSinceBalance, 0);

    // The default mode is Planning, and this month's figures are fresh in any
    // mode.
    QCOMPARE(static_cast<int>(snapshot.profile.trackingMode),
             static_cast<int>(TrackingMode::Planning));
    QCOMPARE(static_cast<int>(snapshot.freshness), static_cast<int>(InputFreshness::Fresh));
}

void TestDashboard::balanceMonthIsTheLaterOfTheTwoSides()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
    household.liabilities.stored.push_back(makeLiability(
        "bolan", "Mortgage", LiabilityKind::Mortgage, Percentage::fromBasisPoints(420)));

    // The fund was valued two months ago and the mortgage last month: the
    // household's balances were last looked at last month, when it updated the
    // larger of the two sides.
    household.valuations.upsert("fund", kSnapshotMonth.plusMonths(-2), sek(1000000));
    household.debtBalances.upsert("bolan", kSnapshotMonth.plusMonths(-1), sek(50000000));

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QVERIFY(snapshot.latestBalanceMonth.has_value());
    QCOMPARE(QString::fromStdString(snapshot.latestBalanceMonth->toString()),
             QStringLiteral("2026-02"));
    QCOMPARE(*snapshot.monthsSinceBalance, 1);
    // No flow is recorded, so the pair is incomplete whatever the balances say.
    QCOMPARE(static_cast<int>(snapshot.freshness), static_cast<int>(InputFreshness::Missing));
}

void TestDashboard::emptyHouseholdHasNoProvenance()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    QVERIFY(!snapshot.latestFlowMonth.has_value());
    QVERIFY(!snapshot.latestBalanceMonth.has_value());
    QVERIFY(!snapshot.monthsSinceFlow.has_value());
    QVERIFY(!snapshot.monthsSinceBalance.has_value());
    QCOMPARE(static_cast<int>(snapshot.freshness), static_cast<int>(InputFreshness::Missing));

    // So is a snapshot that was never built at all, which is what a screen
    // holds before the database is open.
    QCOMPARE(static_cast<int>(DashboardSnapshot{}.freshness),
             static_cast<int>(InputFreshness::Missing));
}

void TestDashboard::theModeDecidesWhatAnOldMonthMeans()
{
    // The same four-month-old budget: a gap in a monthly record, a standing
    // assumption under planning. Nothing but the profile setting differs.
    const auto householdFourMonthsBehind = [](TrackingMode mode) {
        FakeHousehold household;
        household.profiles.stored = sampleProfile();
        household.profiles.stored->trackingMode = mode;
        const YearMonth then = kSnapshotMonth.plusMonths(-4);
        household.flows.stored.push_back(
            MonthFlowRecord{then, flowOf(5000000, 2000000, 400000, 600000)});
        household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
        household.valuations.upsert("fund", then, sek(40000000));
        return household.build(kSnapshotMonth);
    };

    const DashboardSnapshot accounting = householdFourMonthsBehind(TrackingMode::Accounting);
    QCOMPARE(*accounting.monthsSinceFlow, 4);
    QCOMPARE(*accounting.monthsSinceBalance, 4);
    QCOMPARE(static_cast<int>(accounting.freshness), static_cast<int>(InputFreshness::Stale));

    const DashboardSnapshot planning = householdFourMonthsBehind(TrackingMode::Planning);
    QCOMPARE(*planning.monthsSinceFlow, 4);
    QCOMPARE(static_cast<int>(planning.freshness), static_cast<int>(InputFreshness::Fresh));
}

void TestDashboard::aFailedProvenanceReadIsAnError()
{
    // A read that fails must not come back as "nothing was ever recorded": the
    // screen would then tell the household to enter figures it already has.
    FakeHousehold noValuations;
    populate(noValuations);
    noValuations.valuations.failMostRecentMonth = true;
    const Result<DashboardSnapshot> withoutValuationMonth =
        noValuations.buildResult(kSnapshotMonth);
    QVERIFY(!withoutValuationMonth.ok());
    QVERIFY(withoutValuationMonth.error().find("valuations") != std::string::npos);

    FakeHousehold noDebtBalances;
    populate(noDebtBalances);
    noDebtBalances.debtBalances.failMostRecentMonth = true;
    const Result<DashboardSnapshot> withoutDebtMonth = noDebtBalances.buildResult(kSnapshotMonth);
    QVERIFY(!withoutDebtMonth.ok());
    QVERIFY(withoutDebtMonth.error().find("debt balances") != std::string::npos);

    FakeHousehold noFlows;
    populate(noFlows);
    noFlows.flows.failRecent = true;
    const Result<DashboardSnapshot> withoutFlowMonth = noFlows.buildResult(kSnapshotMonth);
    QVERIFY(!withoutFlowMonth.ok());
    QVERIFY(withoutFlowMonth.error().find("month flows") != std::string::npos);
}

void TestDashboard::backfilledMonthsAreKeptAndDoNotBecomeTheLatest()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
    household.flows.stored.push_back(
        MonthFlowRecord{kSnapshotMonth, flowOf(5000000, 2000000, 400000, 600000)});
    household.valuations.upsert("fund", kSnapshotMonth, sek(50000000));

    // A month filled in after the fact is an ordinary write: the month is the
    // key, so the past is a key like any other.
    const YearMonth sixMonthsAgo = kSnapshotMonth.plusMonths(-6);
    const Status flow =
        recordMonth(household.flows, sixMonthsAgo, flowOf(4000000, 1600000, 400000, 500000));
    QVERIFY2(flow.ok(), flow.error().c_str());
    const Status valuation =
        recordValuation(household.valuations, "fund", sixMonthsAgo, sek(45000000));
    QVERIFY2(valuation.ok(), valuation.error().c_str());

    const DashboardSnapshot snapshot = household.build(kSnapshotMonth);

    // The snapshot still describes the most recent month, not the oldest one
    // touched: a backfill adds to the record, it does not move it.
    QCOMPARE(snapshot.hasFlow, true);
    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), qint64(2400000));
    QCOMPARE(ore(snapshot.facts.sheet.totalAssets), qint64(50000000));
    QCOMPARE(QString::fromStdString(snapshot.latestFlowMonth->toString()),
             QStringLiteral("2026-03"));
    QCOMPARE(static_cast<int>(snapshot.freshness), static_cast<int>(InputFreshness::Fresh));

    // And the backfilled months are there, under their own month.
    const Result<std::optional<MonthFlow>> olderFlow = household.flows.at(sixMonthsAgo);
    QVERIFY2(olderFlow.ok(), olderFlow.error().c_str());
    QVERIFY(olderFlow.value().has_value());
    QCOMPARE(ore(olderFlow.value()->income), qint64(4000000));

    const Result<std::optional<Money>> olderValuation =
        household.valuations.balanceAt("fund", sixMonthsAgo);
    QVERIFY2(olderValuation.ok(), olderValuation.error().c_str());
    QVERIFY(olderValuation.value().has_value());
    QCOMPARE(ore(*olderValuation.value()), qint64(45000000));

    const Result<std::vector<MoneyPoint>> history = household.valuations.history("fund");
    QVERIFY2(history.ok(), history.error().c_str());
    const std::size_t expectedPoints = 2;
    QCOMPARE(history.value().size(), expectedPoints);
    QCOMPARE(QString::fromStdString(history.value().front().month.toString()),
             QString::fromStdString(sixMonthsAgo.toString()));
}

void TestDashboard::inputStatusNamesAHouseholdWithNothingRecorded()
{
    QCOMPARE(static_cast<int>(inputStatusOf(DashboardSnapshot{}).kind),
             static_cast<int>(InputStatus::Kind::Empty));

    // One side is not nothing: the sentence has to say which half is missing,
    // because the two halves are entered on two different pages.
    DashboardSnapshot balancesOnly;
    balancesOnly.latestBalanceMonth = YearMonth{2026, 2};
    balancesOnly.monthsSinceBalance = 1;
    const InputStatus noFlow = inputStatusOf(balancesOnly);
    QCOMPARE(static_cast<int>(noFlow.kind), static_cast<int>(InputStatus::Kind::NoFlow));
    QVERIFY(noFlow.balanceMonth.has_value());
    QVERIFY(!noFlow.flowMonth.has_value());

    DashboardSnapshot flowOnly;
    flowOnly.latestFlowMonth = YearMonth{2026, 3};
    flowOnly.monthsSinceFlow = 0;
    QCOMPARE(static_cast<int>(inputStatusOf(flowOnly).kind),
             static_cast<int>(InputStatus::Kind::NoBalances));
}

void TestDashboard::inputStatusKeepsTwoMonthsAsTwoMonths()
{
    // Balances from February and cashflow from March is not "current for
    // March": it is two dates, and the screen has to be able to say both.
    DashboardSnapshot snapshot;
    snapshot.latestBalanceMonth = YearMonth{2026, 2};
    snapshot.latestFlowMonth = YearMonth{2026, 3};
    snapshot.monthsSinceBalance = 1;
    snapshot.monthsSinceFlow = 0;

    const InputStatus status = inputStatusOf(snapshot);
    QCOMPARE(static_cast<int>(status.kind), static_cast<int>(InputStatus::Kind::TwoMonths));
    QVERIFY(status.balanceMonth.has_value());
    QVERIFY(status.flowMonth.has_value());
    QCOMPARE(QString::fromStdString(status.balanceMonth->toString()), QStringLiteral("2026-02"));
    QCOMPARE(QString::fromStdString(status.flowMonth->toString()), QStringLiteral("2026-03"));
}

void TestDashboard::inputStatusOnlyCallsAMonthCurrentWhenBothSidesCarryIt()
{
    DashboardSnapshot snapshot;
    snapshot.latestBalanceMonth = YearMonth{2026, 3};
    snapshot.latestFlowMonth = YearMonth{2026, 3};
    snapshot.monthsSinceBalance = 0;
    snapshot.monthsSinceFlow = 0;
    QCOMPARE(static_cast<int>(inputStatusOf(snapshot).kind),
             static_cast<int>(InputStatus::Kind::BothCurrent));

    // The same shared month, counted from a snapshot two months later, is a
    // shared month that is no longer this one.
    snapshot.monthsSinceBalance = 2;
    snapshot.monthsSinceFlow = 2;
    QCOMPARE(static_cast<int>(inputStatusOf(snapshot).kind),
             static_cast<int>(InputStatus::Kind::BothEarlier));

    // A month recorded ahead of the snapshot counts as current, the way the
    // freshness policy reads a negative count.
    snapshot.monthsSinceBalance = -1;
    snapshot.monthsSinceFlow = -1;
    QCOMPARE(static_cast<int>(inputStatusOf(snapshot).kind),
             static_cast<int>(InputStatus::Kind::BothCurrent));
}

QTEST_APPLESS_MAIN(TestDashboard)

#include "tst_dashboard.moc"
