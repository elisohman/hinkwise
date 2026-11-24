// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/dashboard.h"
#include "application/examples.h"
#include "application/ports.h"
#include "application/result.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/insight.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"
#include "infrastructure/sqlite/sqlite_transaction.h"

#include <QSqlDatabase>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::application;
using namespace hinkwise::domain;
using namespace hinkwise::infrastructure;

namespace {

constexpr YearMonth kMonth{2026, 1};

/// Whole kronor as each story states them, in the minor units the model stores.
std::int64_t kronor(std::int64_t whole)
{
    return whole * 100;
}

std::int64_t ore(const Money& money)
{
    return money.minorUnits();
}

std::int64_t bucketBalance(const DashboardSnapshot& snapshot, Bucket bucket)
{
    return ore(snapshot.facts.sheet.perBucket[static_cast<std::size_t>(bucket)]);
}

bool hasInsight(const DashboardSnapshot& snapshot, std::string_view id)
{
    return std::any_of(snapshot.insights.cbegin(), snapshot.insights.cend(),
                       [id](const Insight& insight) { return insight.id == id; });
}

bool hasSeverity(const DashboardSnapshot& snapshot, Severity severity)
{
    return std::any_of(snapshot.insights.cbegin(), snapshot.insights.cend(),
                       [severity](const Insight& insight) { return insight.severity == severity; });
}

const Asset* assetNamed(const std::vector<Asset>& assets, const std::string& name)
{
    const auto found = std::find_if(assets.cbegin(), assets.cend(),
                                    [&name](const Asset& asset) { return asset.name == name; });
    return found == assets.cend() ? nullptr : &*found;
}

const Liability* liabilityNamed(const std::vector<Liability>& liabilities, const std::string& name)
{
    const auto found =
        std::find_if(liabilities.cbegin(), liabilities.cend(),
                     [&name](const Liability& liability) { return liability.name == name; });
    return found == liabilities.cend() ? nullptr : &*found;
}

/// The repositories one seeded example is written through. They share the
/// connection the temporary database owns, exactly as the application layer
/// does when it opens the file.
struct Storage {
    explicit Storage(QSqlDatabase connection)
        : profiles(connection)
        , assets(connection)
        , liabilities(connection)
        , flows(connection)
        , valuations(connection)
        , debtBalances(connection)
    {
    }

    void activate(std::int64_t profileId)
    {
        assets.setActiveProfile(profileId);
        liabilities.setActiveProfile(profileId);
        flows.setActiveProfile(profileId);
        valuations.setActiveProfile(profileId);
        debtBalances.setActiveProfile(profileId);
    }

    sqlite::SqliteProfileRepository profiles;
    sqlite::SqliteAssetRepository assets;
    sqlite::SqliteLiabilityRepository liabilities;
    sqlite::SqliteMonthFlowRepository flows;
    sqlite::SqliteValuationRepository valuations;
    sqlite::SqliteDebtBalanceRepository debtBalances;
};

/// Creates the profile and writes the whole story inside one transaction, the
/// same order AppController::createExampleProfile uses. The transaction is
/// opened before the profile row so a failure anywhere leaves nothing behind.
Result<Profile> seedProfile(Storage& storage, QSqlDatabase connection, ExampleProfile kind)
{
    using Outcome = Result<Profile>;

    sqlite::SqliteTransactionFactory factory{connection};
    Result<std::unique_ptr<Transaction>> begun = factory.begin();
    if (!begun.ok())
        return Outcome::err(begun.error());

    const Result<Profile> created = storage.profiles.create(exampleProfile(kind));
    if (!created.ok())
        return Outcome::err(created.error());

    storage.activate(created.value().id);

    const Status seeded =
        seedExample(kind, created.value().id, storage.assets, storage.liabilities, storage.flows,
                    storage.valuations, storage.debtBalances, kMonth);
    if (!seeded.ok())
        return Outcome::err(seeded.error());

    const Status committed = begun.value()->commit();
    if (!committed.ok())
        return Outcome::err(committed.error());
    return created;
}

} // namespace

/// Every example against a temporary database: the profile the story names,
/// the rows it writes and the dashboard figures those rows produce. The point
/// of an example is that it demonstrates the model, so the numbers are
/// asserted exactly rather than as "some data exists".
class TestExamples : public QObject {
    Q_OBJECT

private slots:
    void init();

    void gettingStartedShowsAnUnderfundedBuffer();
    void buildingWealthIsAFundedAccumulator();
    void preservingDrawsOnItsBuckets();
    void financiallyIndependentHasWonTheGame();

private:
    QSqlDatabase connection() const;

    std::optional<QTemporaryDir> m_directory;
    std::optional<sqlite::Database> m_database;
};

void TestExamples::init()
{
    m_database.reset();
    m_directory.emplace();
    QVERIFY(m_directory->isValid());

    auto opened = sqlite::Database::open(m_directory->filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    m_database.emplace(std::move(opened.value()));

    const Status migrated = m_database->migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());
}

QSqlDatabase TestExamples::connection() const
{
    return m_database->connection();
}

void TestExamples::gettingStartedShowsAnUnderfundedBuffer()
{
    Storage storage{connection()};
    const Result<Profile> created =
        seedProfile(storage, connection(), ExampleProfile::GettingStarted);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase), static_cast<int>(Phase::GettingStarted));
    QCOMPARE(static_cast<int>(created.value().trackingMode),
             static_cast<int>(TrackingMode::Accounting));
    QCOMPARE(QString::fromStdString(created.value().displayName),
             QStringLiteral("Example: getting started"));
    QCOMPARE(created.value().baseCurrency, Currency::Sek);
    QCOMPARE(QString::fromStdString(created.value().jurisdiction.id), QStringLiteral("SE"));
    QCOMPARE(created.value().riskTolerance.basisPoints(), 5000);

    const Result<std::vector<Asset>> assets = storage.assets.all();
    QVERIFY2(assets.ok(), assets.error().c_str());
    QCOMPARE(assets.value().size(), std::size_t{3});

    const Asset* buffer = assetNamed(assets.value(), "Buffer account");
    QVERIFY(buffer != nullptr);
    QCOMPARE(static_cast<int>(buffer->kind), static_cast<int>(AssetKind::BankAccount));
    QCOMPARE(static_cast<int>(buffer->bucket), static_cast<int>(Bucket::Buffer));

    const Asset* indexFund = assetNamed(assets.value(), "Index fund");
    QVERIFY(indexFund != nullptr);
    QCOMPARE(static_cast<int>(indexFund->kind), static_cast<int>(AssetKind::Fund));
    QCOMPARE(static_cast<int>(indexFund->bucket), static_cast<int>(Bucket::Growth));

    const Asset* pension = assetNamed(assets.value(), "Occupational pension");
    QVERIFY(pension != nullptr);
    QCOMPARE(static_cast<int>(pension->kind), static_cast<int>(AssetKind::PensionOccupational));
    QCOMPARE(static_cast<int>(pension->bucket), static_cast<int>(Bucket::Preserve));

    // Every holding is valued in the current month, so no screen greets the
    // household with a staleness warning on its first day.
    for (const Asset& asset : assets.value()) {
        const Result<std::optional<Money>> value = storage.valuations.latest(asset.id);
        QVERIFY2(value.ok(), value.error().c_str());
        QVERIFY2(value.value().has_value(), asset.name.c_str());
    }
    QCOMPARE(ore(*storage.valuations.latest(buffer->id).value()), kronor(10000));
    QCOMPARE(ore(*storage.valuations.latest(indexFund->id).value()), kronor(12000));
    QCOMPARE(ore(*storage.valuations.latest(pension->id).value()), kronor(25000));

    QVERIFY(storage.liabilities.all().value().empty());

    const Result<std::vector<MonthFlowRecord>> flows = storage.flows.recent(24);
    QVERIFY2(flows.ok(), flows.error().c_str());
    QCOMPARE(flows.value().size(), std::size_t{12});
    QCOMPARE(flows.value().front().month, kMonth);
    QCOMPARE(ore(flows.value().front().flow.income), kronor(31500));
    QCOMPARE(ore(flows.value().front().flow.needs), kronor(15750));
    QCOMPARE(ore(flows.value().front().flow.wants), kronor(9450));
    QCOMPARE(ore(flows.value().front().flow.saving), kronor(6300));
    QVERIFY(flows.value().front().flow.savingSplit.has_value());
    QCOMPARE(ore(flows.value().front().flow.savingSplit->toBuffer), kronor(3300));
    QCOMPARE(ore(flows.value().front().flow.savingSplit->toInvesting), kronor(3000));
    QCOMPARE(ore(flows.value().front().flow.savingSplit->toAmortisation), kronor(0));

    const Result<DashboardSnapshot> built =
        buildDashboard(storage.profiles, storage.assets, storage.liabilities, storage.debtBalances,
                       storage.valuations, storage.flows, kMonth);
    QVERIFY2(built.ok(), built.error().c_str());
    const DashboardSnapshot& snapshot = built.value();

    QCOMPARE(bucketBalance(snapshot, Bucket::Buffer), kronor(10000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Preserve), kronor(25000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Growth), kronor(12000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Play), kronor(0));

    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), kronor(25200));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), kronor(302400));
    QCOMPARE(static_cast<int>(snapshot.metrics.savingsRate.basisPoints()), 2000);
    QCOMPARE(static_cast<int>(snapshot.metrics.needsShare.basisPoints()), 5000);

    // 10 000 against 25 200 kr of monthly expenses is 0.3968 months: below the
    // accumulating band's 0.5-month floor, which is the lesson of the phase.
    QCOMPARE(static_cast<int>(snapshot.metrics.bufferMonths.basisPoints()), 3968);
    // 22 000 kr of spendable capital against 302 400 kr a year.
    QCOMPARE(static_cast<int>(snapshot.metrics.yearsOfFreedom.basisPoints()), 728);
    QCOMPARE(static_cast<int>(snapshot.metrics.fiProgress.basisPoints()), 29);

    // Exactly one lesson: the buffer needs filling. No critical problem, and no
    // second rule tripping over the same household.
    QCOMPARE(snapshot.insights.size(), std::size_t{1});
    QVERIFY(hasInsight(snapshot, "buffer-underfunded"));
    QVERIFY(!hasSeverity(snapshot, Severity::Critical));
}

void TestExamples::buildingWealthIsAFundedAccumulator()
{
    Storage storage{connection()};
    const Result<Profile> created =
        seedProfile(storage, connection(), ExampleProfile::BuildingWealth);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase), static_cast<int>(Phase::BuildingWealth));
    QCOMPARE(static_cast<int>(created.value().trackingMode),
             static_cast<int>(TrackingMode::Accounting));
    QCOMPARE(QString::fromStdString(created.value().displayName),
             QStringLiteral("Example: building wealth"));

    const Result<std::vector<Asset>> assets = storage.assets.all();
    QVERIFY2(assets.ok(), assets.error().c_str());
    QCOMPARE(assets.value().size(), std::size_t{5});

    const Asset* buffer = assetNamed(assets.value(), "Buffer account");
    QVERIFY(buffer != nullptr);
    QCOMPARE(ore(*storage.valuations.latest(buffer->id).value()), kronor(80000));

    const Asset* flat = assetNamed(assets.value(), "Flat");
    QVERIFY(flat != nullptr);
    QCOMPARE(static_cast<int>(flat->bucket), static_cast<int>(Bucket::Preserve));

    const Asset* play = assetNamed(assets.value(), "Play money");
    QVERIFY(play != nullptr);
    QCOMPARE(static_cast<int>(play->bucket), static_cast<int>(Bucket::Play));
    QCOMPARE(ore(*storage.valuations.latest(play->id).value()), kronor(25000));

    const Result<std::vector<Liability>> liabilities = storage.liabilities.all();
    QVERIFY2(liabilities.ok(), liabilities.error().c_str());
    QCOMPARE(liabilities.value().size(), std::size_t{1});
    const Liability* mortgage = liabilityNamed(liabilities.value(), "Mortgage");
    QVERIFY(mortgage != nullptr);
    QCOMPARE(static_cast<int>(mortgage->kind), static_cast<int>(LiabilityKind::Mortgage));
    QCOMPARE(mortgage->interestRate.partsPer100k(), 2650);
    QCOMPARE(static_cast<int>(mortgage->style),
             static_cast<int>(RepaymentStyle::FixedAmortisation));
    QCOMPARE(ore(mortgage->regularPayment), kronor(3000));

    const Result<std::optional<Money>> mortgageBalance = storage.debtBalances.latest(mortgage->id);
    QVERIFY2(mortgageBalance.ok(), mortgageBalance.error().c_str());
    QVERIFY(mortgageBalance.value().has_value());
    QCOMPARE(ore(*mortgageBalance.value()), kronor(1800000));

    const Result<std::vector<MonthFlowRecord>> flows = storage.flows.recent(24);
    QVERIFY2(flows.ok(), flows.error().c_str());
    QCOMPARE(flows.value().size(), std::size_t{12});
    QCOMPARE(flows.value().front().month, kMonth);
    QCOMPARE(ore(flows.value().front().flow.income), kronor(30000));
    QCOMPARE(ore(flows.value().front().flow.needs), kronor(15000));
    QCOMPARE(ore(flows.value().front().flow.wants), kronor(9000));
    QCOMPARE(ore(flows.value().front().flow.saving), kronor(6000));
    QVERIFY(flows.value().front().flow.savingSplit.has_value());
    QCOMPARE(ore(flows.value().front().flow.savingSplit->toAmortisation), kronor(3000));

    const Result<DashboardSnapshot> built =
        buildDashboard(storage.profiles, storage.assets, storage.liabilities, storage.debtBalances,
                       storage.valuations, storage.flows, kMonth);
    QVERIFY2(built.ok(), built.error().c_str());
    const DashboardSnapshot& snapshot = built.value();

    QCOMPARE(bucketBalance(snapshot, Bucket::Buffer), kronor(80000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Preserve), kronor(3100000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Growth), kronor(350000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Play), kronor(25000));

    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), kronor(24000));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), kronor(288000));
    QCOMPARE(static_cast<int>(snapshot.metrics.savingsRate.basisPoints()), 2000);
    QCOMPARE(static_cast<int>(snapshot.metrics.bufferMonths.basisPoints()), 33333);
    QCOMPARE(static_cast<int>(snapshot.metrics.yearsOfFreedom.basisPoints()), 15799);
    QCOMPARE(static_cast<int>(snapshot.metrics.fiProgress.basisPoints()), 632);

    // The buffer is inside its band, play is under its cap, the mortgage is
    // cheap and smaller than the flat: nothing asks this household to act.
    QVERIFY(snapshot.insights.empty());
}

void TestExamples::preservingDrawsOnItsBuckets()
{
    Storage storage{connection()};
    const Result<Profile> created = seedProfile(storage, connection(), ExampleProfile::Preserving);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase), static_cast<int>(Phase::Preserving));
    QCOMPARE(static_cast<int>(created.value().trackingMode),
             static_cast<int>(TrackingMode::Planning));
    QCOMPARE(QString::fromStdString(created.value().displayName),
             QStringLiteral("Example: preserving"));

    const Result<std::vector<Asset>> assets = storage.assets.all();
    QVERIFY2(assets.ok(), assets.error().c_str());
    QCOMPARE(assets.value().size(), std::size_t{5});

    const Asset* buffer = assetNamed(assets.value(), "Buffer account");
    QVERIFY(buffer != nullptr);
    QCOMPARE(ore(*storage.valuations.latest(buffer->id).value()), kronor(864000));

    const Asset* indexFunds = assetNamed(assets.value(), "Index funds");
    QVERIFY(indexFunds != nullptr);
    QCOMPARE(static_cast<int>(indexFunds->bucket), static_cast<int>(Bucket::Preserve));
    QCOMPARE(ore(*storage.valuations.latest(indexFunds->id).value()), kronor(5000000));

    const Asset* growthFund = assetNamed(assets.value(), "Growth fund");
    QVERIFY(growthFund != nullptr);
    QCOMPARE(static_cast<int>(growthFund->bucket), static_cast<int>(Bucket::Growth));
    QCOMPARE(ore(*storage.valuations.latest(growthFund->id).value()), kronor(500000));

    QVERIFY(storage.liabilities.all().value().empty());

    const Result<std::vector<MonthFlowRecord>> flows = storage.flows.recent(24);
    QVERIFY2(flows.ok(), flows.error().c_str());
    QCOMPARE(flows.value().size(), std::size_t{12});
    QCOMPARE(flows.value().front().month, kMonth);
    QCOMPARE(ore(flows.value().front().flow.income), kronor(24000));
    QCOMPARE(ore(flows.value().front().flow.needs), kronor(14000));
    QCOMPARE(ore(flows.value().front().flow.wants), kronor(10000));
    QCOMPARE(ore(flows.value().front().flow.saving), kronor(0));
    // Spending the capital is not a saving, so there is no split to record.
    QVERIFY(!flows.value().front().flow.savingSplit.has_value());

    const Result<DashboardSnapshot> built =
        buildDashboard(storage.profiles, storage.assets, storage.liabilities, storage.debtBalances,
                       storage.valuations, storage.flows, kMonth);
    QVERIFY2(built.ok(), built.error().c_str());
    const DashboardSnapshot& snapshot = built.value();

    QCOMPARE(bucketBalance(snapshot, Bucket::Buffer), kronor(864000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Preserve), kronor(7200000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Growth), kronor(500000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Play), kronor(25000));

    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), kronor(24000));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), kronor(288000));
    // 864 000 kr is three years of expenses: 36 months of buffer.
    QCOMPARE(static_cast<int>(snapshot.metrics.bufferMonths.basisPoints()), 360000);
    // 6 389 000 kr of spendable capital against 288 000 kr a year, because the
    // flat is housing and does not count in the cautious figure.
    QCOMPARE(static_cast<int>(snapshot.metrics.yearsOfFreedom.basisPoints()), 221840);
    QCOMPARE(static_cast<int>(snapshot.metrics.fiProgress.basisPoints()), 8874);

    // Preserve is exactly 25 annual expenses, the low end of the 25-35 band:
    // inside it, so the phase's preserve-thin warning stays quiet.
    const ResolvedTarget& preserveTarget =
        snapshot.facts.targets[static_cast<std::size_t>(Bucket::Preserve)];
    QCOMPARE(ore(preserveTarget.band.low), kronor(7200000));
    QCOMPARE(ore(preserveTarget.band.high), kronor(10080000));
    QVERIFY(preserveTarget.band.contains(
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(Bucket::Preserve)]));

    // The phase spends from the buckets rather than filling them.
    QCOMPARE(static_cast<int>(policyFor(snapshot.profile.phase).flow()),
             static_cast<int>(FlowDirection::OutOfBuckets));

    QVERIFY(!hasSeverity(snapshot, Severity::Critical));
    QVERIFY(!hasSeverity(snapshot, Severity::Warning));
}

void TestExamples::financiallyIndependentHasWonTheGame()
{
    Storage storage{connection()};
    const Result<Profile> created =
        seedProfile(storage, connection(), ExampleProfile::FinanciallyIndependent);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase),
             static_cast<int>(Phase::FinanciallyIndependent));
    QCOMPARE(static_cast<int>(created.value().trackingMode),
             static_cast<int>(TrackingMode::Planning));
    QCOMPARE(QString::fromStdString(created.value().displayName),
             QStringLiteral("Example: financially independent"));

    const Result<std::vector<Asset>> assets = storage.assets.all();
    QVERIFY2(assets.ok(), assets.error().c_str());
    QCOMPARE(assets.value().size(), std::size_t{2});

    const Asset* buffer = assetNamed(assets.value(), "Buffer account");
    QVERIFY(buffer != nullptr);
    QCOMPARE(static_cast<int>(buffer->bucket), static_cast<int>(Bucket::Buffer));
    QCOMPARE(ore(*storage.valuations.latest(buffer->id).value()), kronor(1440000));

    const Asset* indexFunds = assetNamed(assets.value(), "Index funds");
    QVERIFY(indexFunds != nullptr);
    QCOMPARE(static_cast<int>(indexFunds->bucket), static_cast<int>(Bucket::Preserve));
    QCOMPARE(ore(*storage.valuations.latest(indexFunds->id).value()), kronor(7200000));

    // No property, no growth, no play and no debt: the game is won and the
    // household has stopped playing.
    QVERIFY(storage.liabilities.all().value().empty());

    const Result<std::vector<MonthFlowRecord>> flows = storage.flows.recent(24);
    QVERIFY2(flows.ok(), flows.error().c_str());
    QCOMPARE(flows.value().size(), std::size_t{12});
    QCOMPARE(flows.value().front().month, kMonth);
    QCOMPARE(ore(flows.value().front().flow.income), kronor(24000));
    QCOMPARE(ore(flows.value().front().flow.needs), kronor(14000));
    QCOMPARE(ore(flows.value().front().flow.wants), kronor(10000));
    QCOMPARE(ore(flows.value().front().flow.saving), kronor(0));
    QVERIFY(!flows.value().front().flow.savingSplit.has_value());

    const Result<DashboardSnapshot> built =
        buildDashboard(storage.profiles, storage.assets, storage.liabilities, storage.debtBalances,
                       storage.valuations, storage.flows, kMonth);
    QVERIFY2(built.ok(), built.error().c_str());
    const DashboardSnapshot& snapshot = built.value();

    QCOMPARE(bucketBalance(snapshot, Bucket::Buffer), kronor(1440000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Preserve), kronor(7200000));
    QCOMPARE(bucketBalance(snapshot, Bucket::Growth), kronor(0));
    QCOMPARE(bucketBalance(snapshot, Bucket::Play), kronor(0));

    QCOMPARE(ore(snapshot.metrics.monthlyExpenses), kronor(24000));
    QCOMPARE(ore(snapshot.metrics.annualExpenses), kronor(288000));
    // Five years of expenses is 60 months of buffer, and the 8 640 000 kr of
    // capital is 30 years at 288 000 kr a year.
    QCOMPARE(static_cast<int>(snapshot.metrics.bufferMonths.basisPoints()), 600000);
    QCOMPARE(static_cast<int>(snapshot.metrics.yearsOfFreedom.basisPoints()), 300000);
    QVERIFY(snapshot.metrics.fiProgress.basisPoints() >= 10000);
    // 8 640 000 kr against the 7 200 000 kr default target is 120 %.
    QCOMPARE(static_cast<int>(snapshot.metrics.fiProgress.basisPoints()), 12000);

    const ResolvedTarget& preserveTarget =
        snapshot.facts.targets[static_cast<std::size_t>(Bucket::Preserve)];
    QVERIFY(preserveTarget.band.contains(
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(Bucket::Preserve)]));

    QVERIFY(!hasSeverity(snapshot, Severity::Critical));
    QVERIFY(!hasSeverity(snapshot, Severity::Warning));
}

QTEST_GUILESS_MAIN(TestExamples)

#include "tst_examples.moc"
