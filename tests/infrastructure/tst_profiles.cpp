// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/ports.h"
#include "domain/asset.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::infrastructure;

namespace {

constexpr domain::YearMonth kMonth{2026, 1};

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
}

domain::Profile household(const std::string& name, domain::Currency currency,
                          domain::TrackingMode mode)
{
    domain::Profile profile;
    profile.displayName = name;
    profile.baseCurrency = currency;
    profile.phase = domain::Phase::BuildingWealth;
    profile.jurisdiction = domain::swedishJurisdiction();
    profile.trackingMode = mode;
    return profile;
}

domain::MonthFlow flowOf(qint64 income, qint64 needs, qint64 wants, qint64 saving)
{
    domain::MonthFlow flow;
    flow.income = sek(income);
    flow.needs = sek(needs);
    flow.wants = sek(wants);
    flow.saving = sek(saving);
    return flow;
}

int countWhere(QSqlDatabase connection, const QString& table, std::int64_t profileId)
{
    QSqlQuery query{connection};
    query.prepare(QStringLiteral("SELECT count(*) FROM %1 WHERE profile_id = ?").arg(table));
    query.addBindValue(static_cast<qlonglong>(profileId));
    if (!query.exec() || !query.next())
        return -1;
    return query.value(0).toInt();
}

int countAll(QSqlDatabase connection, const QString& table)
{
    QSqlQuery query{connection};
    if (!query.exec(QStringLiteral("SELECT count(*) FROM %1").arg(table)) || !query.next())
        return -1;
    return query.value(0).toInt();
}

} // namespace

/// Several households in one file: the profile repository chooses one, and the
/// scoped repositories read and write only that one. The rules under test are
/// the ones that keep a second household from seeing or destroying the first.
class TestProfiles : public QObject {
    Q_OBJECT

private slots:
    void init();

    void listNamesEveryHousehold();
    void createAllocatesAnIdAndMakesItActive();
    void twoProfilesHoldDifferentAssets();
    void monthFlowsForTheSameMonthCoexist();
    void setActiveChangesWhatEveryReadReturns();
    void readingBeforeAnActiveProfileIsAnError();
    void writingBeforeAnActiveProfileIsAnError();
    void deletingAProfileRemovesAllOfItsData();
    void deletingTheLastProfileIsRefused();
    void interruptedDeleteLeavesTheProfileAndThePointerIntact();
    void danglingActivePointerRepairsToTheFirstHousehold();
    void valuationCannotCrossProfiles();
    void mostRecentMonthIgnoresAnotherHouseholdsDates();

private:
    QSqlDatabase connection() const;

    std::optional<QTemporaryDir> m_directory;
    std::optional<sqlite::Database> m_database;
};

void TestProfiles::init()
{
    m_database.reset();
    m_directory.emplace();
    QVERIFY(m_directory->isValid());

    auto opened = sqlite::Database::open(m_directory->filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    m_database.emplace(std::move(opened.value()));

    const application::Status migrated = m_database->migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());
}

QSqlDatabase TestProfiles::connection() const
{
    return m_database->connection();
}

void TestProfiles::listNamesEveryHousehold()
{
    sqlite::SqliteProfileRepository profiles{connection()};

    const application::Result<domain::Profile> monthly = profiles.create(
        household("Monthly", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(monthly.ok(), monthly.error().c_str());
    QCOMPARE(monthly.value().id != 0, true);

    const application::Result<domain::Profile> budget =
        profiles.create(household("Budget", domain::Currency::Eur, domain::TrackingMode::Planning));
    QVERIFY2(budget.ok(), budget.error().c_str());
    QCOMPARE(budget.value().id > monthly.value().id, true);

    const application::Result<std::vector<application::ProfileSummary>> listed = profiles.list();
    QVERIFY2(listed.ok(), listed.error().c_str());
    QCOMPARE(listed.value().size(), std::size_t{2});

    QCOMPARE(listed.value().at(0).id, monthly.value().id);
    QCOMPARE(QString::fromStdString(listed.value().at(0).displayName), QStringLiteral("Monthly"));
    QCOMPARE(QString::fromStdString(listed.value().at(0).baseCurrency), QStringLiteral("SEK"));
    QCOMPARE(QString::fromStdString(listed.value().at(0).trackingMode),
             QStringLiteral("accounting"));

    QCOMPARE(listed.value().at(1).id, budget.value().id);
    QCOMPARE(QString::fromStdString(listed.value().at(1).displayName), QStringLiteral("Budget"));
    QCOMPARE(QString::fromStdString(listed.value().at(1).baseCurrency), QStringLiteral("EUR"));
    QCOMPARE(QString::fromStdString(listed.value().at(1).trackingMode), QStringLiteral("planning"));
}

void TestProfiles::createAllocatesAnIdAndMakesItActive()
{
    sqlite::SqliteProfileRepository profiles{connection()};

    const application::Result<domain::Profile> first =
        profiles.create(household("First", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(first.ok(), first.error().c_str());
    QCOMPARE(first.value().id, std::int64_t{1});

    const application::Result<std::optional<domain::Profile>> active = profiles.load();
    QVERIFY2(active.ok(), active.error().c_str());
    QVERIFY(active.value().has_value());
    QCOMPARE(active.value()->id, first.value().id);
    QCOMPARE(QString::fromStdString(active.value()->displayName), QStringLiteral("First"));

    // A second create becomes the active household, and the first one stays.
    const application::Result<domain::Profile> second =
        profiles.create(household("Second", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(second.ok(), second.error().c_str());

    const application::Result<std::optional<domain::Profile>> switched = profiles.load();
    QVERIFY2(switched.ok(), switched.error().c_str());
    QVERIFY(switched.value().has_value());
    QCOMPARE(switched.value()->id, second.value().id);
    QCOMPARE(QString::fromStdString(switched.value()->displayName), QStringLiteral("Second"));
}

void TestProfiles::twoProfilesHoldDifferentAssets()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};

    const application::Result<domain::Profile> monthly = profiles.create(
        household("Monthly", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(monthly.ok(), monthly.error().c_str());

    assets.setActiveProfile(monthly.value().id);
    QVERIFY(
        assets.upsert(domain::makeAsset("monthly-fund", "Household fund", domain::AssetKind::Fund))
            .ok());

    const application::Result<domain::Profile> budget =
        profiles.create(household("Budget", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(budget.ok(), budget.error().c_str());

    assets.setActiveProfile(budget.value().id);
    QVERIFY(
        assets.upsert(domain::makeAsset("budget-fund", "Budget reserve", domain::AssetKind::Fund))
            .ok());

    // Each household sees its own row and nothing else.
    const application::Result<std::vector<domain::Asset>> budgetAssets = assets.all();
    QVERIFY2(budgetAssets.ok(), budgetAssets.error().c_str());
    QCOMPARE(budgetAssets.value().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(budgetAssets.value()[0].name),
             QStringLiteral("Budget reserve"));

    assets.setActiveProfile(monthly.value().id);
    const application::Result<std::vector<domain::Asset>> monthlyAssets = assets.all();
    QVERIFY2(monthlyAssets.ok(), monthlyAssets.error().c_str());
    QCOMPARE(monthlyAssets.value().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(monthlyAssets.value()[0].name),
             QStringLiteral("Household fund"));

    // Removing one household's asset leaves the other household's row alone.
    QVERIFY(assets.remove("monthly-fund").ok());
    assets.setActiveProfile(budget.value().id);
    const application::Result<std::vector<domain::Asset>> afterRemoval = assets.all();
    QVERIFY2(afterRemoval.ok(), afterRemoval.error().c_str());
    QCOMPARE(afterRemoval.value().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(afterRemoval.value()[0].id), QStringLiteral("budget-fund"));
}

void TestProfiles::monthFlowsForTheSameMonthCoexist()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteMonthFlowRepository flows{connection()};

    const application::Result<domain::Profile> monthly = profiles.create(
        household("Monthly", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(monthly.ok(), monthly.error().c_str());
    flows.setActiveProfile(monthly.value().id);
    QVERIFY(flows.upsert(kMonth, flowOf(3000000, 1500000, 500000, 1000000)).ok());

    const application::Result<domain::Profile> budget =
        profiles.create(household("Budget", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(budget.ok(), budget.error().c_str());
    flows.setActiveProfile(budget.value().id);
    QVERIFY(flows.upsert(kMonth, flowOf(2000000, 1000000, 400000, 600000)).ok());

    // The same month is two rows, one per household, and each read gets its
    // own.
    const application::Result<std::optional<domain::MonthFlow>> budgetFlow = flows.at(kMonth);
    QVERIFY2(budgetFlow.ok(), budgetFlow.error().c_str());
    QVERIFY(budgetFlow.value().has_value());
    QCOMPARE(budgetFlow.value()->income.minorUnits(), qint64{2000000});

    flows.setActiveProfile(monthly.value().id);
    const application::Result<std::optional<domain::MonthFlow>> monthlyFlow = flows.at(kMonth);
    QVERIFY2(monthlyFlow.ok(), monthlyFlow.error().c_str());
    QVERIFY(monthlyFlow.value().has_value());
    QCOMPARE(monthlyFlow.value()->income.minorUnits(), qint64{3000000});

    // Correcting one household's month does not touch the other one's.
    QVERIFY(flows.upsert(kMonth, flowOf(3100000, 1500000, 500000, 1100000)).ok());
    flows.setActiveProfile(budget.value().id);
    const application::Result<std::optional<domain::MonthFlow>> budgetAgain = flows.at(kMonth);
    QVERIFY2(budgetAgain.ok(), budgetAgain.error().c_str());
    QCOMPARE(budgetAgain.value()->income.minorUnits(), qint64{2000000});

    const application::Result<std::vector<application::MonthFlowRecord>> recent = flows.recent(5);
    QVERIFY2(recent.ok(), recent.error().c_str());
    QCOMPARE(recent.value().size(), std::size_t{1});
}

void TestProfiles::setActiveChangesWhatEveryReadReturns()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteMonthFlowRepository flows{connection()};

    const application::Result<domain::Profile> monthly = profiles.create(
        household("Monthly", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(monthly.ok(), monthly.error().c_str());
    assets.setActiveProfile(monthly.value().id);
    flows.setActiveProfile(monthly.value().id);
    QVERIFY(
        assets.upsert(domain::makeAsset("monthly-fund", "Monthly fund", domain::AssetKind::Fund))
            .ok());
    QVERIFY(flows.upsert(kMonth, flowOf(3000000, 1500000, 500000, 1000000)).ok());

    const application::Result<domain::Profile> budget =
        profiles.create(household("Budget", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(budget.ok(), budget.error().c_str());
    assets.setActiveProfile(budget.value().id);
    flows.setActiveProfile(budget.value().id);
    QVERIFY(assets.upsert(domain::makeAsset("budget-fund", "Budget fund", domain::AssetKind::Fund))
                .ok());

    // The repository pointer and the stored pointer are one choice: setActive
    // moves both, and load() reads the same household the scoped reads do.
    QVERIFY(profiles.setActive(monthly.value().id).ok());
    QCOMPARE(profiles.load().value()->id, monthly.value().id);
    assets.setActiveProfile(monthly.value().id);
    flows.setActiveProfile(monthly.value().id);
    QCOMPARE(QString::fromStdString(assets.all().value()[0].name), QStringLiteral("Monthly fund"));

    QVERIFY(profiles.setActive(budget.value().id).ok());
    QCOMPARE(profiles.load().value()->id, budget.value().id);
    assets.setActiveProfile(budget.value().id);
    flows.setActiveProfile(budget.value().id);
    QCOMPARE(QString::fromStdString(assets.all().value()[0].name), QStringLiteral("Budget fund"));
    // The budget household has no recorded month flow, even though the monthly
    // one does for the same month.
    const application::Result<std::optional<domain::MonthFlow>> budgetFlow = flows.at(kMonth);
    QVERIFY2(budgetFlow.ok(), budgetFlow.error().c_str());
    QVERIFY(!budgetFlow.value().has_value());
}

void TestProfiles::readingBeforeAnActiveProfileIsAnError()
{
    // A fresh database holds no household. Every scoped read refuses rather
    // than returning an empty list, which would look like a household that
    // lost its data.
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteLiabilityRepository liabilities{connection()};
    sqlite::SqlitePlannedExpenseRepository expenses{connection()};
    sqlite::SqliteMonthFlowRepository flows{connection()};
    sqlite::SqlitePhaseHistoryRepository history{connection()};

    const application::Result<std::vector<domain::Asset>> assetRows = assets.all();
    QVERIFY(!assetRows.ok());
    QVERIFY(assetRows.error().find("no active profile") != std::string::npos);

    const application::Result<std::vector<domain::Liability>> liabilityRows = liabilities.all();
    QVERIFY(!liabilityRows.ok());
    QVERIFY(liabilityRows.error().find("no active profile") != std::string::npos);

    const application::Result<std::vector<domain::PlannedExpense>> expenseRows = expenses.all();
    QVERIFY(!expenseRows.ok());
    QVERIFY(expenseRows.error().find("no active profile") != std::string::npos);

    const application::Result<std::optional<domain::MonthFlow>> flow = flows.at(kMonth);
    QVERIFY(!flow.ok());
    QVERIFY(flow.error().find("no active profile") != std::string::npos);

    const application::Result<std::vector<application::MonthFlowRecord>> recent = flows.recent(1);
    QVERIFY(!recent.ok());
    QVERIFY(recent.error().find("no active profile") != std::string::npos);

    const application::Result<std::vector<application::PhaseChange>> changes = history.all();
    QVERIFY(!changes.ok());
    QVERIFY(changes.error().find("no active profile") != std::string::npos);
}

void TestProfiles::writingBeforeAnActiveProfileIsAnError()
{
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteMonthFlowRepository flows{connection()};

    const application::Status saved =
        assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund));
    QVERIFY(!saved.ok());
    QVERIFY(saved.error().find("no active profile") != std::string::npos);

    const application::Status recorded = flows.upsert(kMonth, flowOf(1, 2, 3, 4));
    QVERIFY(!recorded.ok());
    QVERIFY(recorded.error().find("no active profile") != std::string::npos);
}

void TestProfiles::deletingAProfileRemovesAllOfItsData()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteValuationRepository valuations{connection()};
    sqlite::SqliteLiabilityRepository liabilities{connection()};
    sqlite::SqliteDebtBalanceRepository balances{connection()};
    sqlite::SqlitePlannedExpenseRepository expenses{connection()};
    sqlite::SqliteMonthFlowRepository flows{connection()};
    sqlite::SqlitePhaseHistoryRepository history{connection()};

    const application::Result<domain::Profile> doomed =
        profiles.create(household("Doomed", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(doomed.ok(), doomed.error().c_str());
    const std::int64_t doomedId = doomed.value().id;

    assets.setActiveProfile(doomedId);
    liabilities.setActiveProfile(doomedId);
    valuations.setActiveProfile(doomedId);
    balances.setActiveProfile(doomedId);
    expenses.setActiveProfile(doomedId);
    flows.setActiveProfile(doomedId);
    history.setActiveProfile(doomedId);

    QVERIFY(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok());
    QVERIFY(valuations.upsert("fund", kMonth, sek(1000000)).ok());
    QVERIFY(liabilities
                .upsert(domain::makeLiability("bolan", "Mortgage", domain::LiabilityKind::Mortgage,
                                              domain::InterestRate::fromPartsPer100k(4200)))
                .ok());
    QVERIFY(balances.upsert("bolan", kMonth, sek(250000000)).ok());

    domain::PlannedExpense roof;
    roof.id = "roof";
    roof.name = "New roof";
    roof.amount = sek(25000000);
    roof.dueMonth = domain::YearMonth{2027, 5};
    QVERIFY(expenses.upsert(roof).ok());

    QVERIFY(flows.upsert(kMonth, flowOf(3000000, 1500000, 500000, 1000000)).ok());
    QVERIFY(
        history.append(application::PhaseChange{"2026-01-31T12:00:00Z", domain::Phase::Preserving})
            .ok());

    // A second household keeps the deletion legal, and its own row is what
    // proves the cascade did not take everything.
    const application::Result<domain::Profile> survivor = profiles.create(
        household("Survivor", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(survivor.ok(), survivor.error().c_str());
    assets.setActiveProfile(survivor.value().id);
    QVERIFY(
        assets.upsert(domain::makeAsset("buffer", "Buffer", domain::AssetKind::BankAccount)).ok());

    // Deleting the household that was being read hands the active pointer to
    // the one that remains.
    QVERIFY(profiles.setActive(doomedId).ok());
    const application::Status removed = profiles.remove(doomedId);
    QVERIFY2(removed.ok(), removed.error().c_str());

    QCOMPARE(countWhere(connection(), QStringLiteral("asset"), doomedId), 0);
    QCOMPARE(countWhere(connection(), QStringLiteral("liability"), doomedId), 0);
    QCOMPARE(countWhere(connection(), QStringLiteral("planned_expense"), doomedId), 0);
    QCOMPARE(countWhere(connection(), QStringLiteral("month_flow"), doomedId), 0);
    QCOMPARE(countWhere(connection(), QStringLiteral("phase_history"), doomedId), 0);

    // The children hang off the asset and liability ids, so they went with the
    // rows that owned them.
    QCOMPARE(countAll(connection(), QStringLiteral("valuation")), 0);
    QCOMPARE(countAll(connection(), QStringLiteral("debt_balance")), 0);

    // The survivor's asset is untouched, and its id is now the active one.
    QCOMPARE(countWhere(connection(), QStringLiteral("asset"), survivor.value().id), 1);
    const application::Result<std::optional<domain::Profile>> active = profiles.load();
    QVERIFY2(active.ok(), active.error().c_str());
    QVERIFY(active.value().has_value());
    QCOMPARE(active.value()->id, survivor.value().id);
}

void TestProfiles::deletingTheLastProfileIsRefused()
{
    sqlite::SqliteProfileRepository profiles{connection()};

    const application::Result<domain::Profile> only =
        profiles.create(household("Only", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(only.ok(), only.error().c_str());

    const application::Status removed = profiles.remove(only.value().id);
    QVERIFY(!removed.ok());
    QVERIFY(removed.error().find("last profile") != std::string::npos);

    // Refusing is not deleting: the household is still there and still active.
    const application::Result<std::vector<application::ProfileSummary>> listed = profiles.list();
    QVERIFY2(listed.ok(), listed.error().c_str());
    QCOMPARE(listed.value().size(), std::size_t{1});
    QCOMPARE(profiles.load().value()->id, only.value().id);
}

void TestProfiles::danglingActivePointerRepairsToTheFirstHousehold()
{
    // The pointer can outlive the row it names: an external edit, a tool, or a
    // write interrupted between the two steps. Refusing to open would brick a
    // file whose data is entirely intact, so the load repairs instead.
    sqlite::SqliteProfileRepository profiles{connection()};

    const application::Result<domain::Profile> first =
        profiles.create(household("First", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(first.ok(), first.error().c_str());

    const application::Result<domain::Profile> second =
        profiles.create(household("Second", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(second.ok(), second.error().c_str());

    QSqlQuery dangle{connection()};
    QVERIFY(
        dangle.exec(QStringLiteral("UPDATE meta SET value = '9999' WHERE key = 'active_profile'")));

    const application::Result<std::optional<domain::Profile>> loaded = profiles.load();
    QVERIFY2(loaded.ok(), loaded.error().c_str());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->id, first.value().id);

    // Repairing means rewriting the pointer, so the next start is clean.
    QSqlQuery read{connection()};
    QVERIFY(read.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'active_profile'")));
    QVERIFY(read.next());
    QCOMPARE(read.value(0).toLongLong(), static_cast<qlonglong>(first.value().id));
}

void TestProfiles::interruptedDeleteLeavesTheProfileAndThePointerIntact()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};

    const application::Result<domain::Profile> first =
        profiles.create(household("First", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(first.ok(), first.error().c_str());

    assets.setActiveProfile(first.value().id);
    QVERIFY(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok());

    const application::Result<domain::Profile> second =
        profiles.create(household("Second", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(second.ok(), second.error().c_str());
    QVERIFY(profiles.setActive(first.value().id).ok());

    // The delete succeeds and the pointer rewrite is what fails. Both have to
    // be one transaction, or the file would name a profile that is gone.
    QSqlQuery trigger{connection()};
    QVERIFY2(trigger.exec(
                 QStringLiteral("CREATE TRIGGER refuse_active_pointer BEFORE UPDATE ON meta "
                                "WHEN NEW.key = 'active_profile' "
                                "BEGIN SELECT RAISE(ABORT, 'the test refuses the pointer'); END")),
             qPrintable(trigger.lastError().text()));

    const application::Status removed = profiles.remove(first.value().id);
    QVERIFY(!removed.ok());

    // The rollback put back both the row and the pointer, and the data that
    // cascades from the row was never lost.
    const application::Result<std::vector<application::ProfileSummary>> listed = profiles.list();
    QVERIFY2(listed.ok(), listed.error().c_str());
    QCOMPARE(listed.value().size(), std::size_t{2});

    const application::Result<std::optional<domain::Profile>> active = profiles.load();
    QVERIFY2(active.ok(), active.error().c_str());
    QVERIFY(active.value().has_value());
    QCOMPARE(active.value()->id, first.value().id);

    assets.setActiveProfile(first.value().id);
    const application::Result<std::vector<domain::Asset>> rows = assets.all();
    QVERIFY2(rows.ok(), rows.error().c_str());
    QCOMPARE(rows.value().size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(rows.value()[0].id), QStringLiteral("fund"));
}

void TestProfiles::valuationCannotCrossProfiles()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteValuationRepository valuations{connection()};
    sqlite::SqliteLiabilityRepository liabilities{connection()};
    sqlite::SqliteDebtBalanceRepository balances{connection()};

    const application::Result<domain::Profile> monthly = profiles.create(
        household("Monthly", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(monthly.ok(), monthly.error().c_str());
    assets.setActiveProfile(monthly.value().id);
    liabilities.setActiveProfile(monthly.value().id);
    QVERIFY(
        assets.upsert(domain::makeAsset("monthly-fund", "Monthly fund", domain::AssetKind::Fund))
            .ok());
    QVERIFY(
        liabilities
            .upsert(domain::makeLiability("monthly-loan", "Loan", domain::LiabilityKind::Mortgage,
                                          domain::InterestRate::fromPartsPer100k(3000)))
            .ok());

    valuations.setActiveProfile(monthly.value().id);
    balances.setActiveProfile(monthly.value().id);
    QVERIFY(valuations.upsert("monthly-fund", kMonth, sek(1000000)).ok());
    QVERIFY(balances.upsert("monthly-loan", kMonth, sek(500000)).ok());

    const application::Result<domain::Profile> budget =
        profiles.create(household("Budget", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(budget.ok(), budget.error().c_str());
    assets.setActiveProfile(budget.value().id);
    liabilities.setActiveProfile(budget.value().id);
    QVERIFY(assets.upsert(domain::makeAsset("budget-fund", "Budget fund", domain::AssetKind::Fund))
                .ok());
    QVERIFY(
        liabilities
            .upsert(domain::makeLiability("budget-loan", "Loan", domain::LiabilityKind::Mortgage,
                                          domain::InterestRate::fromPartsPer100k(3000)))
            .ok());

    // The budget household can see neither the monthly household's history nor
    // write into it, even with the id it happens to know.
    valuations.setActiveProfile(budget.value().id);
    balances.setActiveProfile(budget.value().id);

    const application::Status crossValuation =
        valuations.upsert("monthly-fund", domain::YearMonth{2026, 2}, sek(1));
    QVERIFY(!crossValuation.ok());
    QVERIFY(crossValuation.error().find("another profile") != std::string::npos);

    const application::Status crossBalance =
        balances.upsert("monthly-loan", domain::YearMonth{2026, 2}, sek(1));
    QVERIFY(!crossBalance.ok());
    QVERIFY(crossBalance.error().find("another profile") != std::string::npos);

    QVERIFY(!valuations.balanceAt("monthly-fund", kMonth).ok());
    QVERIFY(!valuations.latest("monthly-fund").ok());
    QVERIFY(!valuations.history("monthly-fund").ok());
    QVERIFY(!balances.balanceAt("monthly-loan", kMonth).ok());
    QVERIFY(!balances.latest("monthly-loan").ok());
    QVERIFY(!balances.history("monthly-loan").ok());

    // An account that was never recorded has no points rather than an error,
    // which is what the update flow reads when it opens a fresh holding.
    const application::Result<std::vector<application::MoneyPoint>> empty =
        valuations.history("budget-fund");
    QVERIFY2(empty.ok(), empty.error().c_str());
    QVERIFY(empty.value().empty());

    // The monthly household's own read and write still work, untouched.
    valuations.setActiveProfile(monthly.value().id);
    const application::Result<std::optional<domain::Money>> own =
        valuations.balanceAt("monthly-fund", kMonth);
    QVERIFY2(own.ok(), own.error().c_str());
    QVERIFY(own.value().has_value());
    QCOMPARE(own.value()->minorUnits(), qint64{1000000});
}

void TestProfiles::mostRecentMonthIgnoresAnotherHouseholdsDates()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteLiabilityRepository liabilities{connection()};
    sqlite::SqliteValuationRepository valuations{connection()};
    sqlite::SqliteDebtBalanceRepository balances{connection()};

    const application::Result<domain::Profile> recent = profiles.create(
        household("Recent", domain::Currency::Sek, domain::TrackingMode::Accounting));
    QVERIFY2(recent.ok(), recent.error().c_str());
    assets.setActiveProfile(recent.value().id);
    liabilities.setActiveProfile(recent.value().id);
    valuations.setActiveProfile(recent.value().id);
    balances.setActiveProfile(recent.value().id);
    QVERIFY(assets.upsert(domain::makeAsset("recent-fund", "Fund", domain::AssetKind::Fund)).ok());
    QVERIFY(
        liabilities
            .upsert(domain::makeLiability("recent-loan", "Loan", domain::LiabilityKind::Mortgage,
                                          domain::InterestRate::fromPartsPer100k(3000)))
            .ok());
    QVERIFY(valuations.upsert("recent-fund", domain::YearMonth{2026, 3}, sek(1)).ok());
    QVERIFY(balances.upsert("recent-loan", domain::YearMonth{2026, 3}, sek(1)).ok());

    const application::Result<domain::Profile> older =
        profiles.create(household("Older", domain::Currency::Sek, domain::TrackingMode::Planning));
    QVERIFY2(older.ok(), older.error().c_str());
    assets.setActiveProfile(older.value().id);
    liabilities.setActiveProfile(older.value().id);
    valuations.setActiveProfile(older.value().id);
    balances.setActiveProfile(older.value().id);
    QVERIFY(assets.upsert(domain::makeAsset("older-fund", "Fund", domain::AssetKind::Fund)).ok());
    QVERIFY(liabilities
                .upsert(domain::makeLiability("older-loan", "Loan", domain::LiabilityKind::Mortgage,
                                              domain::InterestRate::fromPartsPer100k(3000)))
                .ok());
    QVERIFY(valuations.upsert("older-fund", domain::YearMonth{2026, 1}, sek(1)).ok());
    QVERIFY(balances.upsert("older-loan", domain::YearMonth{2026, 1}, sek(1)).ok());

    // The older household must not report March just because another
    // household recorded a valuation then.
    const application::Result<std::optional<domain::YearMonth>> olderValuation =
        valuations.mostRecentMonth();
    QVERIFY2(olderValuation.ok(), olderValuation.error().c_str());
    QVERIFY(olderValuation.value().has_value());
    QCOMPARE(QString::fromStdString(olderValuation.value()->toString()), QStringLiteral("2026-01"));

    const application::Result<std::optional<domain::YearMonth>> olderDebt =
        balances.mostRecentMonth();
    QVERIFY2(olderDebt.ok(), olderDebt.error().c_str());
    QVERIFY(olderDebt.value().has_value());
    QCOMPARE(QString::fromStdString(olderDebt.value()->toString()), QStringLiteral("2026-01"));

    valuations.setActiveProfile(recent.value().id);
    balances.setActiveProfile(recent.value().id);
    QCOMPARE(QString::fromStdString(valuations.mostRecentMonth().value()->toString()),
             QStringLiteral("2026-03"));
    QCOMPARE(QString::fromStdString(balances.mostRecentMonth().value()->toString()),
             QStringLiteral("2026-03"));
}

QTEST_GUILESS_MAIN(TestProfiles)

#include "tst_profiles.moc"
