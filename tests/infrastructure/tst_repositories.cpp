// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/ports.h"
#include "domain/asset.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"

#include <QFile>
#include <QFileDevice>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

using namespace hinkwise;
using namespace hinkwise::infrastructure;

namespace {

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
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

domain::MonthFlow euroFlowOf(qint64 income)
{
    domain::MonthFlow flow;
    flow.income = domain::Money{income, domain::Currency::Eur};
    flow.needs = domain::Money{income / 3, domain::Currency::Eur};
    flow.wants = domain::Money{income / 6, domain::Currency::Eur};
    flow.saving = domain::Money{income / 2, domain::Currency::Eur};
    return flow;
}

} // namespace

class TestRepositories : public QObject {
    Q_OBJECT

private slots:
    void init();

    void profileIsMissingOnAFreshDatabase();
    void profileRoundTrip();
    void assetRoundTrip();
    void assetUpsertOverwrites();
    void liabilityRoundTrip();
    void plannedExpenseRoundTrip();
    void valuationRoundTrip();
    void deletingAnAssetCascadesItsValuations();
    void debtBalanceRoundTrip();
    void monthFlowRoundTrip();
    void monthFlowWithAPartlyStoredBreakdownIsAnError();
    void monthFlowBreakdownThatDoesNotAddUpIsAnError();
    void monthFlowCurrencyMismatchIsAnError();
    void unknownStoredTextIsAnError();
    void databaseFileIsOwnerOnly();
    void reopeningTheSameFileKeepsItsRows();

private:
    QSqlDatabase connection() const;

    std::optional<QTemporaryDir> m_directory;
    std::optional<sqlite::Database> m_database;
    QString m_path;
};

void TestRepositories::init()
{
    // A new directory per test, so no test can read another one's rows.
    m_database.reset();
    m_directory.emplace();
    QVERIFY(m_directory->isValid());

    m_path = m_directory->filePath(QStringLiteral("hinkwise.db"));
    auto opened = sqlite::Database::open(m_path);
    QVERIFY2(opened.ok(), opened.error().c_str());
    m_database.emplace(std::move(opened.value()));

    const application::Status migrated = m_database->migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());
}

QSqlDatabase TestRepositories::connection() const
{
    return m_database->connection();
}

void TestRepositories::profileIsMissingOnAFreshDatabase()
{
    sqlite::SqliteProfileRepository profiles{connection()};

    const application::Result<std::optional<domain::Profile>> loaded = profiles.load();
    QVERIFY2(loaded.ok(), loaded.error().c_str());
    QVERIFY(!loaded.value().has_value());
}

void TestRepositories::profileRoundTrip()
{
    sqlite::SqliteProfileRepository profiles{connection()};

    domain::Profile profile;
    profile.displayName = "Elis";
    profile.baseCurrency = domain::Currency::Eur;
    profile.phase = domain::Phase::Preserving;
    profile.riskTolerance = domain::Percentage::fromBasisPoints(3500);
    profile.jurisdiction =
        domain::customJurisdiction(domain::Percentage::fromBasisPoints(350),
                                   domain::Percentage::fromBasisPoints(80), 23, 28, 36);

    const application::Status saved = profiles.save(profile);
    QVERIFY2(saved.ok(), saved.error().c_str());

    const application::Result<std::optional<domain::Profile>> loaded = profiles.load();
    QVERIFY2(loaded.ok(), loaded.error().c_str());
    QVERIFY(loaded.value().has_value());

    const domain::Profile& stored = *loaded.value();
    QCOMPARE(QString::fromStdString(stored.displayName), QStringLiteral("Elis"));
    QCOMPARE(static_cast<int>(stored.baseCurrency), static_cast<int>(domain::Currency::Eur));
    QCOMPARE(static_cast<int>(stored.phase), static_cast<int>(domain::Phase::Preserving));
    QCOMPARE(stored.riskTolerance.basisPoints(), 3500);
    QCOMPARE(QString::fromStdString(stored.jurisdiction.id), QStringLiteral("custom"));
    QCOMPARE(stored.jurisdiction.safeWithdrawalRate.basisPoints(), 350);
    QCOMPARE(stored.jurisdiction.capitalTaxDrag.basisPoints(), 80);
    QCOMPARE(stored.jurisdiction.fiMultiplierLow, 23);
    QCOMPARE(stored.jurisdiction.fiMultiplierDefault, 28);
    QCOMPARE(stored.jurisdiction.fiMultiplierHigh, 36);

    // created_at records the first save, not the last edit.
    QSqlQuery createdAtQuery{connection()};
    QVERIFY(createdAtQuery.exec(QStringLiteral("SELECT created_at FROM profile WHERE id = 1")));
    QVERIFY(createdAtQuery.next());
    const QString createdAt = createdAtQuery.value(0).toString();
    QVERIFY(!createdAt.isEmpty());

    domain::Profile renamed = profile;
    renamed.displayName = "Elis Ohman";
    renamed.phase = domain::Phase::FinanciallyIndependent;
    QVERIFY(profiles.save(renamed).ok());

    const application::Result<std::optional<domain::Profile>> reloaded = profiles.load();
    QVERIFY2(reloaded.ok(), reloaded.error().c_str());
    QCOMPARE(QString::fromStdString(reloaded.value()->displayName), QStringLiteral("Elis Ohman"));
    QCOMPARE(static_cast<int>(reloaded.value()->phase),
             static_cast<int>(domain::Phase::FinanciallyIndependent));

    QSqlQuery createdAtAgain{connection()};
    QVERIFY(createdAtAgain.exec(QStringLiteral("SELECT created_at FROM profile WHERE id = 1")));
    QVERIFY(createdAtAgain.next());
    QCOMPARE(createdAtAgain.value(0).toString(), createdAt);
}

void TestRepositories::assetRoundTrip()
{
    sqlite::SqliteAssetRepository assets{connection()};

    const domain::Asset fund = domain::makeAsset("fund", "Global fund", domain::AssetKind::Fund);
    domain::Asset flat = domain::withBucket(
        domain::makeAsset("flat", "Flat", domain::AssetKind::RealEstate), domain::Bucket::Growth);
    flat.includedInHeadline = false;

    QVERIFY(assets.upsert(fund).ok());
    QVERIFY(assets.upsert(flat).ok());

    const application::Result<std::vector<domain::Asset>> all = assets.all();
    QVERIFY2(all.ok(), all.error().c_str());

    const std::size_t expectedCount = 2;
    QCOMPARE(all.value().size(), expectedCount);

    // Ordered by name: Flat before Global fund.
    const domain::Asset& first = all.value()[0];
    QCOMPARE(QString::fromStdString(first.id), QStringLiteral("flat"));
    QCOMPARE(QString::fromStdString(first.name), QStringLiteral("Flat"));
    QCOMPARE(static_cast<int>(first.kind), static_cast<int>(domain::AssetKind::RealEstate));
    QCOMPARE(static_cast<int>(first.bucket), static_cast<int>(domain::Bucket::Growth));
    QCOMPARE(first.liquid, false);
    QCOMPARE(first.includedInHeadline, false);

    const domain::Asset& second = all.value()[1];
    QCOMPARE(QString::fromStdString(second.id), QStringLiteral("fund"));
    QCOMPARE(static_cast<int>(second.kind), static_cast<int>(domain::AssetKind::Fund));
    QCOMPARE(static_cast<int>(second.bucket), static_cast<int>(domain::Bucket::Growth));
    QCOMPARE(second.liquid, true);
    QCOMPARE(second.includedInHeadline, true);
}

void TestRepositories::assetUpsertOverwrites()
{
    sqlite::SqliteAssetRepository assets{connection()};

    QVERIFY(assets.upsert(domain::makeAsset("fund", "Global fund", domain::AssetKind::Fund)).ok());

    domain::Asset renamed =
        domain::makeAsset("fund", "Global index fund", domain::AssetKind::Equity);
    renamed = domain::withBucket(renamed, domain::Bucket::Preserve);
    renamed.includedInHeadline = false;
    QVERIFY(assets.upsert(renamed).ok());

    const application::Result<std::vector<domain::Asset>> all = assets.all();
    QVERIFY2(all.ok(), all.error().c_str());

    const std::size_t expectedCount = 1;
    QCOMPARE(all.value().size(), expectedCount);
    QCOMPARE(QString::fromStdString(all.value()[0].name), QStringLiteral("Global index fund"));
    QCOMPARE(static_cast<int>(all.value()[0].kind), static_cast<int>(domain::AssetKind::Equity));
    QCOMPARE(static_cast<int>(all.value()[0].bucket), static_cast<int>(domain::Bucket::Preserve));
    QCOMPARE(all.value()[0].includedInHeadline, false);
}

void TestRepositories::liabilityRoundTrip()
{
    sqlite::SqliteLiabilityRepository liabilities{connection()};

    const domain::Liability mortgage =
        domain::makeLiability("bolan", "Mortgage", domain::LiabilityKind::Mortgage,
                              domain::Percentage::fromBasisPoints(425));
    QVERIFY(liabilities.upsert(mortgage).ok());

    const application::Result<std::vector<domain::Liability>> all = liabilities.all();
    QVERIFY2(all.ok(), all.error().c_str());

    const std::size_t expectedCount = 1;
    QCOMPARE(all.value().size(), expectedCount);
    QCOMPARE(QString::fromStdString(all.value()[0].id), QStringLiteral("bolan"));
    QCOMPARE(QString::fromStdString(all.value()[0].name), QStringLiteral("Mortgage"));
    QCOMPARE(static_cast<int>(all.value()[0].kind),
             static_cast<int>(domain::LiabilityKind::Mortgage));
    QCOMPARE(all.value()[0].interestRate.basisPoints(), 425);

    const domain::Liability repriced =
        domain::makeLiability("bolan", "Mortgage", domain::LiabilityKind::Mortgage,
                              domain::Percentage::fromBasisPoints(390));
    QVERIFY(liabilities.upsert(repriced).ok());

    const application::Result<std::vector<domain::Liability>> reloaded = liabilities.all();
    QVERIFY2(reloaded.ok(), reloaded.error().c_str());
    QCOMPARE(reloaded.value().size(), expectedCount);
    QCOMPARE(reloaded.value()[0].interestRate.basisPoints(), 390);
}

void TestRepositories::plannedExpenseRoundTrip()
{
    sqlite::SqlitePlannedExpenseRepository expenses{connection()};

    domain::PlannedExpense roof;
    roof.id = "roof";
    roof.name = "New roof";
    roof.amount = sek(25000000);
    roof.dueMonth = domain::YearMonth{2027, 5};
    QVERIFY(expenses.upsert(roof).ok());

    domain::PlannedExpense car;
    car.id = "car";
    car.name = "Car";
    car.amount = domain::Money{18000000, domain::Currency::Eur};
    car.dueMonth = domain::YearMonth{2026, 9};
    QVERIFY(expenses.upsert(car).ok());

    const application::Result<std::vector<domain::PlannedExpense>> all = expenses.all();
    QVERIFY2(all.ok(), all.error().c_str());

    const std::size_t expectedCount = 2;
    QCOMPARE(all.value().size(), expectedCount);

    // Ordered by due month: the car comes before the roof.
    QCOMPARE(QString::fromStdString(all.value()[0].id), QStringLiteral("car"));
    QCOMPARE(all.value()[0].amount.minorUnits(), qint64(18000000));
    QCOMPARE(static_cast<int>(all.value()[0].amount.currency()),
             static_cast<int>(domain::Currency::Eur));
    QCOMPARE(QString::fromStdString(all.value()[0].dueMonth.toString()), QStringLiteral("2026-09"));

    QCOMPARE(QString::fromStdString(all.value()[1].id), QStringLiteral("roof"));
    QCOMPARE(all.value()[1].amount.minorUnits(), qint64(25000000));
    QCOMPARE(QString::fromStdString(all.value()[1].dueMonth.toString()), QStringLiteral("2027-05"));
}

void TestRepositories::valuationRoundTrip()
{
    sqlite::SqliteAssetRepository assets{connection()};
    QVERIFY(assets.upsert(domain::makeAsset("fund", "Global fund", domain::AssetKind::Fund)).ok());

    sqlite::SqliteValuationRepository valuations{connection()};
    QVERIFY(valuations.upsert("fund", domain::YearMonth{2026, 1}, sek(1000000)).ok());
    QVERIFY(valuations.upsert("fund", domain::YearMonth{2026, 3}, sek(1200000)).ok());

    const application::Result<std::optional<domain::Money>> january =
        valuations.balanceAt("fund", domain::YearMonth{2026, 1});
    QVERIFY2(january.ok(), january.error().c_str());
    QVERIFY(january.value().has_value());
    QCOMPARE(january.value()->minorUnits(), qint64(1000000));

    const application::Result<std::optional<domain::Money>> february =
        valuations.balanceAt("fund", domain::YearMonth{2026, 2});
    QVERIFY2(february.ok(), february.error().c_str());
    QVERIFY(!february.value().has_value());

    const application::Result<std::optional<domain::Money>> latest = valuations.latest("fund");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QVERIFY(latest.value().has_value());
    QCOMPARE(latest.value()->minorUnits(), qint64(1200000));

    // The same month can be corrected; the row is replaced, not duplicated.
    QVERIFY(valuations.upsert("fund", domain::YearMonth{2026, 3}, sek(1250000)).ok());

    const application::Result<std::optional<domain::Money>> corrected = valuations.latest("fund");
    QVERIFY2(corrected.ok(), corrected.error().c_str());
    QCOMPARE(corrected.value()->minorUnits(), qint64(1250000));

    QSqlQuery count{connection()};
    QVERIFY(count.exec(QStringLiteral("SELECT count(*) FROM valuation WHERE asset_id = 'fund'")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 2);

    // The whole history, oldest first: a backup is written from this, and
    // latest() alone would lose the months before it.
    const application::Result<std::vector<application::MoneyPoint>> history =
        valuations.history("fund");
    QVERIFY2(history.ok(), history.error().c_str());
    QCOMPARE(history.value().size(), std::size_t{2});
    QCOMPARE(QString::fromStdString(history.value().at(0).month.toString()),
             QStringLiteral("2026-01"));
    QCOMPARE(history.value().at(0).amount.minorUnits(), qint64(1000000));
    QCOMPARE(QString::fromStdString(history.value().at(1).month.toString()),
             QStringLiteral("2026-03"));
    QCOMPARE(history.value().at(1).amount.minorUnits(), qint64(1250000));

    // An asset that was never valued has no points, which is not an error.
    const application::Result<std::vector<application::MoneyPoint>> nothing =
        valuations.history("missing");
    QVERIFY2(nothing.ok(), nothing.error().c_str());
    QVERIFY(nothing.value().empty());
}

void TestRepositories::deletingAnAssetCascadesItsValuations()
{
    sqlite::SqliteAssetRepository assets{connection()};
    sqlite::SqliteValuationRepository valuations{connection()};

    QVERIFY(assets.upsert(domain::makeAsset("fund", "Global fund", domain::AssetKind::Fund)).ok());
    QVERIFY(valuations.upsert("fund", domain::YearMonth{2026, 1}, sek(1000000)).ok());
    QVERIFY(valuations.upsert("fund", domain::YearMonth{2026, 2}, sek(1100000)).ok());

    const application::Status removed = assets.remove("fund");
    QVERIFY2(removed.ok(), removed.error().c_str());

    const application::Result<std::vector<domain::Asset>> all = assets.all();
    QVERIFY2(all.ok(), all.error().c_str());
    QVERIFY(all.value().empty());

    const application::Result<std::optional<domain::Money>> latest = valuations.latest("fund");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QVERIFY(!latest.value().has_value());

    QSqlQuery count{connection()};
    QVERIFY(count.exec(QStringLiteral("SELECT count(*) FROM valuation")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);

    // Deleting what is already gone is not an error.
    QVERIFY(assets.remove("fund").ok());
}

void TestRepositories::debtBalanceRoundTrip()
{
    sqlite::SqliteLiabilityRepository liabilities{connection()};
    QVERIFY(liabilities
                .upsert(domain::makeLiability("bolan", "Mortgage", domain::LiabilityKind::Mortgage,
                                              domain::Percentage::fromBasisPoints(420)))
                .ok());

    sqlite::SqliteDebtBalanceRepository balances{connection()};
    QVERIFY(balances.upsert("bolan", domain::YearMonth{2026, 1}, sek(250000000)).ok());
    QVERIFY(balances.upsert("bolan", domain::YearMonth{2026, 2}, sek(247500000)).ok());

    const application::Result<std::optional<domain::Money>> january =
        balances.balanceAt("bolan", domain::YearMonth{2026, 1});
    QVERIFY2(january.ok(), january.error().c_str());
    QVERIFY(january.value().has_value());
    QCOMPARE(january.value()->minorUnits(), qint64(250000000));

    const application::Result<std::optional<domain::Money>> latest = balances.latest("bolan");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QVERIFY(latest.value().has_value());
    QCOMPARE(latest.value()->minorUnits(), qint64(247500000));

    QVERIFY(balances.upsert("bolan", domain::YearMonth{2026, 2}, sek(247000000)).ok());
    const application::Result<std::optional<domain::Money>> corrected = balances.latest("bolan");
    QVERIFY2(corrected.ok(), corrected.error().c_str());
    QCOMPARE(corrected.value()->minorUnits(), qint64(247000000));

    // Every recorded balance, oldest first, with the corrected month in place.
    const application::Result<std::vector<application::MoneyPoint>> history =
        balances.history("bolan");
    QVERIFY2(history.ok(), history.error().c_str());
    QCOMPARE(history.value().size(), std::size_t{2});
    QCOMPARE(QString::fromStdString(history.value().at(0).month.toString()),
             QStringLiteral("2026-01"));
    QCOMPARE(history.value().at(0).amount.minorUnits(), qint64(250000000));
    QCOMPARE(history.value().at(1).amount.minorUnits(), qint64(247000000));

    // A debt nobody has recorded a balance for has no points, not an error.
    const application::Result<std::vector<application::MoneyPoint>> nothing =
        balances.history("missing");
    QVERIFY2(nothing.ok(), nothing.error().c_str());
    QVERIFY(nothing.value().empty());
}

void TestRepositories::monthFlowRoundTrip()
{
    sqlite::SqliteMonthFlowRepository flows{connection()};

    // January records where its 10 000 kr of saving went; February records only
    // the total, which is what a month written before the breakdown has.
    domain::MonthFlow january = flowOf(3000000, 1500000, 500000, 1000000);
    january.savingSplit = domain::SavingsBreakdown{sek(400000), sek(400000), sek(200000)};
    const domain::MonthFlow february = flowOf(3100000, 1400000, 600000, 1100000);
    QVERIFY(flows.upsert(domain::YearMonth{2026, 1}, january).ok());
    QVERIFY(flows.upsert(domain::YearMonth{2026, 2}, february).ok());

    const application::Result<std::optional<domain::MonthFlow>> at =
        flows.at(domain::YearMonth{2026, 1});
    QVERIFY2(at.ok(), at.error().c_str());
    QVERIFY(at.value().has_value());
    QCOMPARE(at.value()->income.minorUnits(), qint64(3000000));
    QCOMPARE(at.value()->needs.minorUnits(), qint64(1500000));
    QCOMPARE(at.value()->wants.minorUnits(), qint64(500000));
    QCOMPARE(at.value()->saving.minorUnits(), qint64(1000000));
    QVERIFY(at.value()->savingSplit.has_value());
    QCOMPARE(at.value()->savingSplit->toBuffer.minorUnits(), qint64(400000));
    QCOMPARE(at.value()->savingSplit->toInvesting.minorUnits(), qint64(400000));
    QCOMPARE(at.value()->savingSplit->toAmortisation.minorUnits(), qint64(200000));
    QCOMPARE(static_cast<int>(at.value()->savingSplit->toBuffer.currency()),
             static_cast<int>(domain::Currency::Sek));

    // A month with no breakdown comes back with none, not with three zeroes.
    const application::Result<std::optional<domain::MonthFlow>> withoutSplit =
        flows.at(domain::YearMonth{2026, 2});
    QVERIFY2(withoutSplit.ok(), withoutSplit.error().c_str());
    QVERIFY(withoutSplit.value().has_value());
    QVERIFY(!withoutSplit.value()->savingSplit.has_value());

    // The columns are NULL for that row, which is what "not broken down" looks
    // like on disk and what an older release left behind.
    QSqlQuery nulls{connection()};
    QVERIFY(nulls.exec(QStringLiteral("SELECT saving_buffer_minor, saving_invest_minor, "
                                      "saving_amort_minor FROM month_flow "
                                      "WHERE month = '2026-02'")));
    QVERIFY(nulls.next());
    for (int column = 0; column < 3; ++column)
        QVERIFY(nulls.value(column).isNull());

    const application::Result<std::optional<domain::MonthFlow>> missing =
        flows.at(domain::YearMonth{2025, 12});
    QVERIFY2(missing.ok(), missing.error().c_str());
    QVERIFY(!missing.value().has_value());

    const application::Result<std::vector<application::MonthFlowRecord>> recent = flows.recent(5);
    QVERIFY2(recent.ok(), recent.error().c_str());

    const std::size_t expectedCount = 2;
    QCOMPARE(recent.value().size(), expectedCount);
    QCOMPARE(QString::fromStdString(recent.value()[0].month.toString()), QStringLiteral("2026-02"));
    QCOMPARE(recent.value()[0].flow.income.minorUnits(), qint64(3100000));
    QVERIFY(!recent.value()[0].flow.savingSplit.has_value());
    QCOMPARE(QString::fromStdString(recent.value()[1].month.toString()), QStringLiteral("2026-01"));
    QVERIFY(recent.value()[1].flow.savingSplit.has_value());
    QCOMPARE(recent.value()[1].flow.savingSplit->toAmortisation.minorUnits(), qint64(200000));

    const application::Result<std::vector<application::MonthFlowRecord>> onlyOne = flows.recent(1);
    QVERIFY2(onlyOne.ok(), onlyOne.error().c_str());
    QCOMPARE(onlyOne.value().size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(onlyOne.value()[0].month.toString()),
             QStringLiteral("2026-02"));

    // Correcting a month replaces the row, breakdown included.
    domain::MonthFlow correctedJanuary = flowOf(3200000, 1500000, 500000, 1200000);
    correctedJanuary.savingSplit = domain::SavingsBreakdown{sek(600000), sek(400000), sek(200000)};
    QVERIFY(flows.upsert(domain::YearMonth{2026, 1}, correctedJanuary).ok());

    const application::Result<std::optional<domain::MonthFlow>> recorrected =
        flows.at(domain::YearMonth{2026, 1});
    QVERIFY2(recorrected.ok(), recorrected.error().c_str());
    QCOMPARE(recorrected.value()->income.minorUnits(), qint64(3200000));
    QCOMPARE(recorrected.value()->savingSplit->toBuffer.minorUnits(), qint64(600000));

    // Re-saving a month without a breakdown clears the three columns, so a
    // corrected month cannot keep a breakdown nobody stands behind.
    const domain::MonthFlow noBreakdown = flowOf(3200000, 1500000, 500000, 1200000);
    QVERIFY(flows.upsert(domain::YearMonth{2026, 1}, noBreakdown).ok());
    const application::Result<std::optional<domain::MonthFlow>> cleared =
        flows.at(domain::YearMonth{2026, 1});
    QVERIFY2(cleared.ok(), cleared.error().c_str());
    QVERIFY(!cleared.value()->savingSplit.has_value());

    const application::Result<std::vector<application::MonthFlowRecord>> stillTwo = flows.recent(5);
    QVERIFY2(stillTwo.ok(), stillTwo.error().c_str());
    QCOMPARE(stillTwo.value().size(), expectedCount);
}

/// A row where only some of the three columns are filled in is not something
/// any writer produces. It is reported rather than read as "no breakdown",
/// because that would drop the parts that are there.
void TestRepositories::monthFlowWithAPartlyStoredBreakdownIsAnError()
{
    QSqlQuery insert{connection()};
    QVERIFY(insert.exec(
        QStringLiteral("INSERT INTO month_flow (month, income_minor, needs_minor, wants_minor, "
                       "saving_minor, currency, saving_buffer_minor) VALUES "
                       "('2026-03', 3000000, 1500000, 500000, 1000000, 'SEK', 400000)")));

    sqlite::SqliteMonthFlowRepository flows{connection()};

    const application::Result<std::optional<domain::MonthFlow>> at =
        flows.at(domain::YearMonth{2026, 3});
    QVERIFY(!at.ok());
    QVERIFY(at.error().find("2026-03") != std::string::npos);
    QVERIFY(at.error().find("breakdown") != std::string::npos);

    const application::Result<std::vector<application::MonthFlowRecord>> recent = flows.recent(5);
    QVERIFY(!recent.ok());
    QVERIFY(recent.error().find("2026-03") != std::string::npos);
}

/// The breakdown is a breakdown of the total: a stored one that does not add up
/// is corrupt data, and reading it back as if it were true would show the
/// household a split that contradicts its own saving.
void TestRepositories::monthFlowBreakdownThatDoesNotAddUpIsAnError()
{
    QSqlQuery insert{connection()};
    QVERIFY(insert.exec(
        QStringLiteral("INSERT INTO month_flow (month, income_minor, needs_minor, wants_minor, "
                       "saving_minor, currency, saving_buffer_minor, saving_invest_minor, "
                       "saving_amort_minor) VALUES "
                       "('2026-04', 3000000, 1500000, 500000, 1000000, 'SEK', 400000, "
                       "400000, 100000)")));

    sqlite::SqliteMonthFlowRepository flows{connection()};

    const application::Result<std::optional<domain::MonthFlow>> at =
        flows.at(domain::YearMonth{2026, 4});
    QVERIFY(!at.ok());
    QVERIFY(at.error().find("2026-04") != std::string::npos);

    // The write side refuses it too, so the application cannot store the lie in
    // the first place.
    domain::MonthFlow broken = flowOf(3000000, 1500000, 500000, 1000000);
    broken.savingSplit = domain::SavingsBreakdown{sek(400000), sek(400000), sek(100000)};
    const application::Status saved = flows.upsert(domain::YearMonth{2026, 5}, broken);
    QVERIFY(!saved.ok());
    QVERIFY(saved.error().find("breakdown") != std::string::npos);
}

void TestRepositories::monthFlowCurrencyMismatchIsAnError()
{
    sqlite::SqliteMonthFlowRepository flows{connection()};

    // A single month in another currency is not a problem in itself.
    const domain::MonthFlow january = euroFlowOf(3000000);
    QVERIFY(flows.upsert(domain::YearMonth{2026, 1}, january).ok());

    const application::Result<std::optional<domain::MonthFlow>> at =
        flows.at(domain::YearMonth{2026, 1});
    QVERIFY2(at.ok(), at.error().c_str());
    QVERIFY(at.value().has_value());

    const domain::MonthFlow february = flowOf(3000000, 1500000, 500000, 1000000);
    QVERIFY(flows.upsert(domain::YearMonth{2026, 2}, february).ok());

    // Two currencies in one series cannot be charted or added up. The read
    // reports that as a message; nothing throws.
    const application::Result<std::vector<application::MonthFlowRecord>> recent = flows.recent(5);
    QVERIFY(!recent.ok());
    QVERIFY(recent.error().find("EUR") != std::string::npos);
    QVERIFY(recent.error().find("SEK") != std::string::npos);
}

void TestRepositories::unknownStoredTextIsAnError()
{
    // Text the domain does not recognise - a hand-edited file, or a row from a
    // future release - is reported, not thrown and not guessed.
    QSqlQuery insert{connection()};
    QVERIFY(insert.exec(QStringLiteral("INSERT INTO asset (id, name, kind, bucket, liquid, "
                                       "included_in_headline) VALUES ('x', 'Mystery', "
                                       "'space-ship', 'growth', 1, 1)")));

    sqlite::SqliteAssetRepository assets{connection()};
    const application::Result<std::vector<domain::Asset>> all = assets.all();
    QVERIFY(!all.ok());
    QVERIFY(all.error().find("space-ship") != std::string::npos);
}

void TestRepositories::databaseFileIsOwnerOnly()
{
    const QFile::Permissions permissions = QFile::permissions(m_path);

    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));
}

void TestRepositories::reopeningTheSameFileKeepsItsRows()
{
    domain::Profile profile;
    profile.displayName = "Elis";
    profile.baseCurrency = domain::Currency::Sek;
    profile.phase = domain::Phase::GettingStarted;
    profile.jurisdiction = domain::swedishJurisdiction();

    {
        sqlite::SqliteProfileRepository before{connection()};
        const application::Status saved = before.save(profile);
        QVERIFY2(saved.ok(), saved.error().c_str());
    }

    // Opening the path a second time must open what is there, not truncate it
    // into a new empty file.
    m_database.reset();

    auto reopened = sqlite::Database::open(m_path);
    QVERIFY2(reopened.ok(), reopened.error().c_str());
    m_database.emplace(std::move(reopened.value()));

    const application::Status migrated = m_database->migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());

    sqlite::SqliteProfileRepository after{m_database->connection()};
    const application::Result<std::optional<domain::Profile>> loaded = after.load();
    QVERIFY2(loaded.ok(), loaded.error().c_str());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(QString::fromStdString(loaded.value()->displayName), QStringLiteral("Elis"));
}

QTEST_GUILESS_MAIN(TestRepositories)

#include "tst_repositories.moc"
