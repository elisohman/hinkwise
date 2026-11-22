// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/dashboard.h"
#include "application/ports.h"
#include "application/result.h"
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

class FakeValuationRepository final : public IValuationRepository {
public:
    std::map<std::string, Money> latestBalance;

    Result<std::optional<Money>> balanceAt(std::string_view, YearMonth) const override
    {
        return Result<std::optional<Money>>::ok(std::nullopt);
    }

    Result<std::optional<Money>> latest(std::string_view assetId) const override
    {
        const auto found = latestBalance.find(std::string{assetId});
        if (found == latestBalance.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(found->second);
    }

    Status upsert(std::string_view assetId, YearMonth, Money balance) override
    {
        latestBalance[std::string{assetId}] = balance;
        return Status::success();
    }
};

class FakeDebtBalanceRepository final : public IDebtBalanceRepository {
public:
    std::map<std::string, Money> latestBalance;

    Result<std::optional<Money>> balanceAt(std::string_view, YearMonth) const override
    {
        return Result<std::optional<Money>>::ok(std::nullopt);
    }

    Result<std::optional<Money>> latest(std::string_view liabilityId) const override
    {
        const auto found = latestBalance.find(std::string{liabilityId});
        if (found == latestBalance.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(found->second);
    }

    Status upsert(std::string_view liabilityId, YearMonth, Money balance) override
    {
        latestBalance[std::string{liabilityId}] = balance;
        return Status::success();
    }
};

class FakeMonthFlowRepository final : public IMonthFlowRepository {
public:
    std::vector<MonthFlowRecord> stored;

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

    DashboardSnapshot build(YearMonth month)
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

    household.valuations.latestBalance["buffer"] = sek(1000000);
    household.valuations.latestBalance["fund"] = sek(40000000);
    household.valuations.latestBalance["flat"] = sek(200000000);
    household.valuations.latestBalance["crypto"] = sek(1000000);

    household.debtBalances.latestBalance["mortgage"] = sek(150000000);
    household.debtBalances.latestBalance["card"] = sek(2000000);

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
    void assetWithoutValuationCountsAsZero();
    void flowIsUsedWhenItIsNotLaterThanTheSnapshotMonth();
    void flowLaterThanTheSnapshotMonthIsNotPartOfIt();
    void totalBadLoansSumsOnlyDebtsAboveTheBenchmark();
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

void TestDashboard::assetWithoutValuationCountsAsZero()
{
    FakeHousehold household;
    household.profiles.stored = sampleProfile();
    household.assets.stored.push_back(
        makeAsset("buffer", "Buffer account", AssetKind::BankAccount));
    household.assets.stored.push_back(makeAsset("fund", "Global fund", AssetKind::Fund));
    household.valuations.latestBalance["buffer"] = sek(1000000);

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

QTEST_APPLESS_MAIN(TestDashboard)

#include "tst_dashboard.moc"
