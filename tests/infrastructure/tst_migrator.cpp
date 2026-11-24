// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/migrator.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::infrastructure;

namespace {

/// The version a fresh file reaches, which is also the number the application
/// has to keep in step with schemaMigrations().
constexpr int kExpectedVersion = 8;

constexpr std::array<const char*, 9> kTables{
    "profile",    "asset",         "liability", "planned_expense", "valuation", "debt_balance",
    "month_flow", "phase_history", "meta",
};

int tableCount(QSqlDatabase& connection, const char* name)
{
    QSqlQuery query{connection};
    query.prepare(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'table' "
                                 "AND name = ?"));
    query.addBindValue(QString::fromUtf8(name));
    if (!query.exec() || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool hasColumn(QSqlDatabase& connection, const char* table, const char* column)
{
    QSqlQuery query{connection};
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(QString::fromUtf8(table))))
        return false;
    while (query.next()) {
        if (query.value(1).toString() == QString::fromUtf8(column))
            return true;
    }
    return false;
}

bool isNotNull(QSqlDatabase& connection, const char* table, const char* column)
{
    QSqlQuery query{connection};
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(QString::fromUtf8(table))))
        return false;
    while (query.next()) {
        if (query.value(1).toString() == QString::fromUtf8(column))
            return query.value(3).toInt() != 0;
    }
    return false;
}

/// The three columns migration 2 adds, in table order.
constexpr std::array<const char*, 3> kSavingBreakdownColumns{
    "saving_buffer_minor",
    "saving_invest_minor",
    "saving_amort_minor",
};

/// The three columns migration 4 adds, in table order.
constexpr std::array<const char*, 3> kLoanTermsColumns{
    "repayment_style",
    "regular_payment_minor",
    "monthly_fee_minor",
};

/// The liability row a version 5 file holds: the schema before a loan's rate
/// gained a third decimal. Its rate is still stated in basis points.
constexpr const char* kInsertVersionFiveLiability =
    "INSERT INTO liability (id, name, kind, interest_bp, repayment_style, "
    "regular_payment_minor, monthly_fee_minor, term_months, archived) VALUES "
    "('bolan', 'Mortgage', 'mortgage', 265, 'fixed-amortisation', 0, 0, 0, 0)";

/// The liability row a version 4 file holds: the schema before a loan could
/// state how long it still has to run.
constexpr const char* kInsertVersionFourLiability =
    "INSERT INTO liability (id, name, kind, interest_bp, repayment_style, "
    "regular_payment_minor, monthly_fee_minor, archived) VALUES "
    "('csn', 'Student loan', 'student-loan', 150, 'annuity', 150000, 5000, 0)";

/// The liability row a version 3 file holds: the schema before the repayment
/// terms existed.
constexpr const char* kInsertVersionThreeLiability =
    "INSERT INTO liability (id, name, kind, interest_bp, archived) VALUES "
    "('bolan', 'Mortgage', 'mortgage', 420, 0)";

/// The profile row a version 2 file holds: the schema before tracking_mode
/// existed.
constexpr const char* kInsertVersionTwoProfile =
    "INSERT INTO profile (id, display_name, base_currency, phase, risk_tolerance_bp, "
    "jurisdiction_id, safe_withdrawal_bp, capital_tax_bp, fi_multiplier_low, "
    "fi_multiplier_default, fi_multiplier_high, created_at) VALUES "
    "(1, 'Elis', 'SEK', 'building-wealth', 5000, 'SE', 400, 100, 21, 25, 33, "
    "'2026-01-31T12:00:00Z')";

/// The profile row a version 6 file holds, before a file could hold more than
/// one household. It is the row the upgrade has to keep as profile 1.
constexpr const char* kInsertVersionSixProfile =
    "INSERT INTO profile (id, display_name, base_currency, phase, risk_tolerance_bp, "
    "jurisdiction_id, safe_withdrawal_bp, capital_tax_bp, fi_multiplier_low, "
    "fi_multiplier_default, fi_multiplier_high, created_at, tracking_mode) VALUES "
    "(1, 'Elis', 'SEK', 'building-wealth', 5000, 'SE', 400, 100, 21, 25, 33, "
    "'2026-01-31T12:00:00Z', 'accounting')";

/// The profile's example flag, -1 when the column or the row is missing.
int exampleFlagOfStoredProfile(QSqlDatabase& connection)
{
    QSqlQuery query{connection};
    if (!query.exec(QStringLiteral("SELECT is_example FROM profile WHERE id = 1")) || !query.next())
        return -1;
    return query.value(0).toInt();
}

QString trackingModeOfStoredProfile(QSqlDatabase& connection)
{
    QSqlQuery query{connection};
    if (!query.exec(QStringLiteral("SELECT tracking_mode FROM profile WHERE id = 1")) ||
        !query.next())
        return QString{};
    return query.value(0).toString();
}

/// A migration that is allowed to run.
application::Status createProbeTable(QSqlDatabase& database)
{
    QSqlQuery query{database};
    if (!query.exec(QStringLiteral("CREATE TABLE probe (id INTEGER PRIMARY KEY)")))
        return application::Status::err("probe table: " + query.lastError().text().toStdString());
    return application::Status::success();
}

/// A migration that does work and then fails, to prove the work is rolled back.
application::Status createTableAndFail(QSqlDatabase& database)
{
    QSqlQuery query{database};
    if (!query.exec(QStringLiteral("CREATE TABLE half_done (id INTEGER PRIMARY KEY)")))
        return application::Status::err("half_done table: " +
                                        query.lastError().text().toStdString());
    return application::Status::err("deliberate failure");
}

} // namespace

class TestMigrator : public QObject {
    Q_OBJECT

private slots:
    void freshFileMigratesToTheExpectedVersion();
    void versionOneUpgradesAndKeepsItsRows();
    void versionTwoProfileComesOutAsPlanning();
    void versionThreeLiabilityComesOutWithoutTerms();
    void versionFourLiabilityComesOutWithoutATerm();
    void versionFiveLiabilityKeepsItsRate();
    void versionSixUpgradesToOneHouseholdWithItsData();
    void versionSevenProfileLoadsAsNotAnExample();
    void versionSixWithDataButNoProfileIsRefused();
    void migratingTwiceChangesNothing();
    void newerSchemaIsRefused();
    void failedMigrationLeavesTheVersionUnchanged();
    void migrationsMustBeListedInOrder();
};

void TestMigrator::freshFileMigratesToTheExpectedVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());

    const application::Status migrated = database.migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());

    QSqlDatabase connection = database.connection();
    QVERIFY(connection.isOpen());

    const application::Result<int> version = sqlite::schemaVersion(connection);
    QVERIFY2(version.ok(), version.error().c_str());
    QCOMPARE(version.value(), kExpectedVersion);

    for (const char* table : kTables)
        QCOMPARE(tableCount(connection, table), 1);

    // The saving breakdown arrived in version 2 and is part of a fresh file.
    for (const char* column : kSavingBreakdownColumns)
        QVERIFY(hasColumn(connection, "month_flow", column));

    // So did the tracking mode, whose default is the mode a household is in
    // before it has chosen one.
    QVERIFY(hasColumn(connection, "profile", "tracking_mode"));

    // And the loan terms, which a fresh file has room for too.
    for (const char* column : kLoanTermsColumns)
        QVERIFY(hasColumn(connection, "liability", column));

    // So is the remaining term, whose default is the loan that states none.
    QVERIFY(hasColumn(connection, "liability", "term_months"));

    // The rate is stored to three decimals, and the two-decimal column it came
    // from is gone rather than left beside it.
    QVERIFY(hasColumn(connection, "liability", "interest_100k"));
    QVERIFY(!hasColumn(connection, "liability", "interest_bp"));

    // Version 8 says which profiles are examples. A fresh file has the column
    // and no profile yet; the wizard's first household reads it as 0, which the
    // version 7 upgrade test proves for a row that already exists.
    QVERIFY(hasColumn(connection, "profile", "is_example"));
    QCOMPARE(exampleFlagOfStoredProfile(connection), -1);

    // Version 7 gives every household-owned table an owner column, and the
    // profile itself no longer insists on being the only row.
    for (const char* table :
         {"asset", "liability", "planned_expense", "month_flow", "phase_history"})
        QVERIFY(hasColumn(connection, table, "profile_id"));
    QSqlQuery profileSql{connection};
    QVERIFY(profileSql.exec(
        QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'profile'")));
    QVERIFY(profileSql.next());
    QVERIFY(!profileSql.value(0).toString().contains(QStringLiteral("CHECK (id = 1)")));
}

/// A file written by the release that only knew schema version 1 has months in
/// it. Upgrading must add the breakdown columns and leave those rows exactly as
/// they were, with NULL where the breakdown would go.
void TestMigrator::versionOneUpgradesAndKeepsItsRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{2});
    const std::span<const sqlite::Migration> versionOneOnly{migrations.data(), 1};

    const application::Result<int> atOne = sqlite::applyMigrations(connection, versionOneOnly);
    QVERIFY2(atOne.ok(), atOne.error().c_str());
    QCOMPARE(atOne.value(), 1);

    // Schemas before version 2 have no breakdown columns at all.
    for (const char* column : kSavingBreakdownColumns)
        QVERIFY(!hasColumn(connection, "month_flow", column));

    // A file that has a recorded month has a household that recorded it. The
    // version 7 upgrade gives every row an owner, so the fixture has to name
    // the one the month belongs to.
    QSqlQuery insertProfile{connection};
    QVERIFY2(insertProfile.exec(QString::fromUtf8(kInsertVersionTwoProfile)),
             qPrintable(insertProfile.lastError().text()));

    QSqlQuery insert{connection};
    QVERIFY(insert.exec(QStringLiteral("INSERT INTO month_flow (month, income_minor, needs_minor, "
                                       "wants_minor, saving_minor, currency) VALUES "
                                       "('2026-01', 3000000, 1500000, 500000, 1000000, 'SEK')")));

    const application::Result<int> atTwo = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atTwo.ok(), atTwo.error().c_str());
    QCOMPARE(atTwo.value(), kExpectedVersion);

    const application::Result<int> stored = sqlite::schemaVersion(connection);
    QVERIFY2(stored.ok(), stored.error().c_str());
    QCOMPARE(stored.value(), kExpectedVersion);

    for (const char* column : kSavingBreakdownColumns)
        QVERIFY(hasColumn(connection, "month_flow", column));

    // The month survived the upgrade, and its breakdown is absent rather than
    // zero: the household never recorded one.
    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT income_minor, needs_minor, wants_minor, "
                                       "saving_minor, currency, saving_buffer_minor, "
                                       "saving_invest_minor, saving_amort_minor "
                                       "FROM month_flow WHERE month = '2026-01'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toLongLong(), qint64(3000000));
    QCOMPARE(select.value(1).toLongLong(), qint64(1500000));
    QCOMPARE(select.value(2).toLongLong(), qint64(500000));
    QCOMPARE(select.value(3).toLongLong(), qint64(1000000));
    QCOMPARE(select.value(4).toString(), QStringLiteral("SEK"));
    for (int column = 5; column < 8; ++column)
        QVERIFY(select.value(column).isNull());

    // And running the migrator again is still a no-op on the upgraded file.
    const application::Result<int> again = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(again.ok(), again.error().c_str());
    QCOMPARE(again.value(), kExpectedVersion);
    QVERIFY(hasColumn(connection, "month_flow", "saving_buffer_minor"));
}

/// A version 2 file can already hold a household. Upgrading has to give that
/// one a tracking mode, and the only defensible value is Planning: Accounting
/// would turn its next quiet month into a gap it never agreed to keep.
void TestMigrator::versionTwoProfileComesOutAsPlanning()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{3});
    const std::span<const sqlite::Migration> upToVersionTwo{migrations.data(), 2};

    const application::Result<int> atTwo = sqlite::applyMigrations(connection, upToVersionTwo);
    QVERIFY2(atTwo.ok(), atTwo.error().c_str());
    QCOMPARE(atTwo.value(), 2);
    QVERIFY(!hasColumn(connection, "profile", "tracking_mode"));

    QSqlQuery insert{connection};
    QVERIFY2(insert.exec(QString::fromUtf8(kInsertVersionTwoProfile)),
             qPrintable(insert.lastError().text()));

    const application::Result<int> atThree = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atThree.ok(), atThree.error().c_str());
    QCOMPARE(atThree.value(), kExpectedVersion);

    QVERIFY(hasColumn(connection, "profile", "tracking_mode"));

    // The household that was already there has a mode now, and it is the one
    // that reads its figures as a standing assumption.
    QCOMPARE(trackingModeOfStoredProfile(connection), QStringLiteral("planning"));

    // The rest of the row is untouched.
    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT display_name, base_currency FROM profile")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Elis"));
    QCOMPARE(select.value(1).toString(), QStringLiteral("SEK"));

    // And a second run leaves the chosen mode alone rather than resetting it,
    // which is what makes the upgrade safe to repeat on every start.
    QSqlQuery chose{connection};
    QVERIFY(
        chose.exec(QStringLiteral("UPDATE profile SET tracking_mode = 'accounting' WHERE id = 1")));
    const application::Result<int> again = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(again.ok(), again.error().c_str());
    QCOMPARE(again.value(), kExpectedVersion);
    QCOMPARE(trackingModeOfStoredProfile(connection), QStringLiteral("accounting"));
}

/// A version 3 file can already hold a loan. Upgrading has to give it terms,
/// and the only defensible ones are fixed amortisation with a zero payment:
/// that is the model's "no terms recorded yet", not a loan being paid off. The
/// row's name and rate must survive untouched.
void TestMigrator::versionThreeLiabilityComesOutWithoutTerms()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{4});
    const std::span<const sqlite::Migration> upToVersionThree{migrations.data(), 3};

    const application::Result<int> atThree = sqlite::applyMigrations(connection, upToVersionThree);
    QVERIFY2(atThree.ok(), atThree.error().c_str());
    QCOMPARE(atThree.value(), 3);
    for (const char* column : kLoanTermsColumns)
        QVERIFY(!hasColumn(connection, "liability", column));

    QSqlQuery insert{connection};
    QVERIFY2(insert.exec(QString::fromUtf8(kInsertVersionThreeLiability)),
             qPrintable(insert.lastError().text()));
    // A version 6 file could hold household rows without a profile, but then
    // migration 7 rightly refuses. The fixture is a real file, so it names the
    // household the loan belongs to.
    QSqlQuery profile{connection};
    QVERIFY2(profile.exec(QString::fromUtf8(kInsertVersionTwoProfile)),
             qPrintable(profile.lastError().text()));

    const application::Result<int> atFour = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atFour.ok(), atFour.error().c_str());
    QCOMPARE(atFour.value(), kExpectedVersion);
    for (const char* column : kLoanTermsColumns)
        QVERIFY(hasColumn(connection, "liability", column));

    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT name, interest_100k, repayment_style, "
                                       "regular_payment_minor, monthly_fee_minor "
                                       "FROM liability WHERE id = 'bolan'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Mortgage"));
    // 4.20 % in basis points becomes 4.200 % in parts per 100 000.
    QCOMPARE(select.value(1).toInt(), 4200);
    QCOMPARE(select.value(2).toString(), QStringLiteral("fixed-amortisation"));
    QCOMPARE(select.value(3).toLongLong(), qint64(0));
    QCOMPARE(select.value(4).toLongLong(), qint64(0));

    // Running the migrator again leaves the filled-in terms alone.
    QSqlQuery chose{connection};
    QVERIFY(chose.exec(QStringLiteral("UPDATE liability SET repayment_style = 'annuity', "
                                      "regular_payment_minor = 1750000, "
                                      "monthly_fee_minor = 5000 WHERE id = 'bolan'")));
    const application::Result<int> again = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(again.ok(), again.error().c_str());
    QCOMPARE(again.value(), kExpectedVersion);
    select.finish();
    QVERIFY(select.exec(QStringLiteral("SELECT repayment_style, regular_payment_minor "
                                       "FROM liability WHERE id = 'bolan'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("annuity"));
    QCOMPARE(select.value(1).toLongLong(), qint64(1750000));
}

/// A version 4 file can already hold a loan that records its payments. Upgrading
/// has to give it a term column, and the only defensible value is zero: with no
/// term the plan keeps using the payment the household recorded, exactly as the
/// file was written. The row's terms must survive untouched.
void TestMigrator::versionFourLiabilityComesOutWithoutATerm()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{5});
    const std::span<const sqlite::Migration> upToVersionFour{migrations.data(), 4};

    const application::Result<int> atFour = sqlite::applyMigrations(connection, upToVersionFour);
    QVERIFY2(atFour.ok(), atFour.error().c_str());
    QCOMPARE(atFour.value(), 4);
    QVERIFY(!hasColumn(connection, "liability", "term_months"));

    QSqlQuery insert{connection};
    QVERIFY2(insert.exec(QString::fromUtf8(kInsertVersionFourLiability)),
             qPrintable(insert.lastError().text()));
    QSqlQuery profile{connection};
    QVERIFY2(profile.exec(QString::fromUtf8(kInsertVersionTwoProfile)),
             qPrintable(profile.lastError().text()));

    const application::Result<int> atFive = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atFive.ok(), atFive.error().c_str());
    QCOMPARE(atFive.value(), kExpectedVersion);
    QVERIFY(hasColumn(connection, "liability", "term_months"));

    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT name, interest_100k, repayment_style, "
                                       "regular_payment_minor, monthly_fee_minor, term_months "
                                       "FROM liability WHERE id = 'csn'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Student loan"));
    QCOMPARE(select.value(1).toInt(), 1500);
    QCOMPARE(select.value(2).toString(), QStringLiteral("annuity"));
    QCOMPARE(select.value(3).toLongLong(), qint64(150000));
    QCOMPARE(select.value(4).toLongLong(), qint64(5000));
    QCOMPARE(select.value(5).toInt(), 0);

    // Running the migrator again leaves a term that was recorded after the
    // upgrade alone, which is what makes the upgrade safe on every start.
    QSqlQuery chose{connection};
    QVERIFY(chose.exec(QStringLiteral("UPDATE liability SET term_months = 96 WHERE id = 'csn'")));
    const application::Result<int> again = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(again.ok(), again.error().c_str());
    QCOMPARE(again.value(), kExpectedVersion);
    select.finish();
    QVERIFY(select.exec(QStringLiteral("SELECT term_months FROM liability WHERE id = 'csn'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toInt(), 96);
}

/// A version 5 file records its loan rates in basis points. Widening the unit
/// to parts per 100 000 must keep the rate exactly: 2.65 % becomes 2.650 %, and
/// the old column is dropped only after the value has been copied.
void TestMigrator::versionFiveLiabilityKeepsItsRate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{6});
    const std::span<const sqlite::Migration> upToVersionFive{migrations.data(), 5};

    const application::Result<int> atFive = sqlite::applyMigrations(connection, upToVersionFive);
    QVERIFY2(atFive.ok(), atFive.error().c_str());
    QCOMPARE(atFive.value(), 5);
    QVERIFY(hasColumn(connection, "liability", "interest_bp"));
    QVERIFY(!hasColumn(connection, "liability", "interest_100k"));

    QSqlQuery insert{connection};
    QVERIFY2(insert.exec(QString::fromUtf8(kInsertVersionFiveLiability)),
             qPrintable(insert.lastError().text()));
    QSqlQuery profile{connection};
    QVERIFY2(profile.exec(QString::fromUtf8(kInsertVersionTwoProfile)),
             qPrintable(profile.lastError().text()));

    const application::Result<int> atSix = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atSix.ok(), atSix.error().c_str());
    QCOMPARE(atSix.value(), kExpectedVersion);
    QVERIFY(hasColumn(connection, "liability", "interest_100k"));
    QVERIFY(!hasColumn(connection, "liability", "interest_bp"));

    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT interest_100k FROM liability WHERE id = 'bolan'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toInt(), 2650);

    // Running the migrator again leaves the widened rate alone.
    const application::Result<int> again = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(again.ok(), again.error().c_str());
    QCOMPARE(again.value(), kExpectedVersion);
    select.finish();
    QVERIFY(select.exec(QStringLiteral("SELECT interest_100k FROM liability WHERE id = 'bolan'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toInt(), 2650);
}

/// A version 6 file holds exactly one household and all of its data. Upgrading
/// must leave that household readable as profile 1: same row, same ids, same
/// balances and flows, now tagged with the profile every scoped read filters on.
void TestMigrator::versionSixUpgradesToOneHouseholdWithItsData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{7});
    const std::span<const sqlite::Migration> upToVersionSix{migrations.data(), 6};

    const application::Result<int> atSix = sqlite::applyMigrations(connection, upToVersionSix);
    QVERIFY2(atSix.ok(), atSix.error().c_str());
    QCOMPARE(atSix.value(), 6);

    const std::array<const char*, 8> rows{
        kInsertVersionSixProfile,
        "INSERT INTO asset (id, name, kind, bucket, liquid, included_in_headline) VALUES "
        "('fund', 'Global fund', 'fund', 'growth', 1, 1)",
        "INSERT INTO valuation (asset_id, month, amount_minor, currency) VALUES "
        "('fund', '2026-01', 1000000, 'SEK')",
        "INSERT INTO liability (id, name, kind, interest_100k, repayment_style, "
        "regular_payment_minor, monthly_fee_minor, term_months, archived) VALUES "
        "('bolan', 'Mortgage', 'mortgage', 4200, 'annuity', 1750000, 5000, 240, 0)",
        "INSERT INTO debt_balance (liability_id, month, amount_minor, currency) VALUES "
        "('bolan', '2026-01', 250000000, 'SEK')",
        "INSERT INTO planned_expense (id, name, amount_minor, due_month, currency) VALUES "
        "('roof', 'New roof', 25000000, '2027-05', 'SEK')",
        "INSERT INTO month_flow (month, income_minor, needs_minor, wants_minor, saving_minor, "
        "currency, saving_buffer_minor, saving_invest_minor, saving_amort_minor) VALUES "
        "('2026-01', 3000000, 1500000, 500000, 1000000, 'SEK', 400000, 400000, 200000)",
        "INSERT INTO phase_history (changed_at, phase) VALUES "
        "('2026-01-31T12:00:00Z', 'building-wealth')",
    };
    for (const char* row : rows) {
        QSqlQuery insert{connection};
        QVERIFY2(insert.exec(QString::fromUtf8(row)), qPrintable(insert.lastError().text()));
    }

    const application::Result<int> atSeven = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atSeven.ok(), atSeven.error().c_str());
    QCOMPARE(atSeven.value(), kExpectedVersion);

    // Every rebuilt owner column is required, so no later write can create an
    // orphan row either.
    for (const char* table :
         {"asset", "liability", "planned_expense", "month_flow", "phase_history"})
        QVERIFY(isNotNull(connection, table, "profile_id"));

    // An insert that names no owner is refused by the schema rather than
    // stored as a row no scoped read can find.
    QSqlQuery ownerless{connection};
    QVERIFY(!ownerless.exec(QStringLiteral(
        "INSERT INTO asset (id, name, kind, bucket, liquid, "
        "included_in_headline) VALUES ('orphan', 'Orphan', 'fund', 'growth', 1, 1)")));

    // The household is still one row with the id it had, and the single-row
    // CHECK is gone so a second one can be created.
    QSqlQuery profile{connection};
    QVERIFY(profile.exec(QStringLiteral("SELECT id, display_name, tracking_mode FROM profile")));
    QVERIFY(profile.next());
    QCOMPARE(profile.value(0).toInt(), 1);
    QCOMPARE(profile.value(1).toString(), QStringLiteral("Elis"));
    QCOMPARE(profile.value(2).toString(), QStringLiteral("accounting"));
    QVERIFY(profile.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type = 'table' "
                                        "AND name = 'profile'")));
    QVERIFY(profile.next());
    QVERIFY(!profile.value(0).toString().contains(QStringLiteral("CHECK (id = 1)")));

    QSqlQuery active{connection};
    QVERIFY(active.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'active_profile'")));
    QVERIFY(active.next());
    QCOMPARE(active.value(0).toString(), QStringLiteral("1"));

    // Every row that existed is still there, under profile 1, with its values.
    QSqlQuery asset{connection};
    QVERIFY(asset.exec(QStringLiteral("SELECT id, name, profile_id FROM asset")));
    QVERIFY(asset.next());
    QCOMPARE(asset.value(0).toString(), QStringLiteral("fund"));
    QCOMPARE(asset.value(1).toString(), QStringLiteral("Global fund"));
    QCOMPARE(asset.value(2).toInt(), 1);

    QSqlQuery valuation{connection};
    QVERIFY(valuation.exec(QStringLiteral("SELECT asset_id, month, amount_minor FROM valuation")));
    QVERIFY(valuation.next());
    QCOMPARE(valuation.value(0).toString(), QStringLiteral("fund"));
    QCOMPARE(valuation.value(1).toString(), QStringLiteral("2026-01"));
    QCOMPARE(valuation.value(2).toLongLong(), qint64(1000000));

    QSqlQuery liability{connection};
    QVERIFY(liability.exec(QStringLiteral(
        "SELECT id, interest_100k, regular_payment_minor, profile_id FROM liability")));
    QVERIFY(liability.next());
    QCOMPARE(liability.value(0).toString(), QStringLiteral("bolan"));
    QCOMPARE(liability.value(1).toInt(), 4200);
    QCOMPARE(liability.value(2).toLongLong(), qint64(1750000));
    QCOMPARE(liability.value(3).toInt(), 1);

    QSqlQuery balance{connection};
    QVERIFY(balance.exec(QStringLiteral("SELECT liability_id, amount_minor FROM debt_balance")));
    QVERIFY(balance.next());
    QCOMPARE(balance.value(0).toString(), QStringLiteral("bolan"));
    QCOMPARE(balance.value(1).toLongLong(), qint64(250000000));

    QSqlQuery expense{connection};
    QVERIFY(expense.exec(QStringLiteral("SELECT id, due_month, profile_id FROM planned_expense")));
    QVERIFY(expense.next());
    QCOMPARE(expense.value(0).toString(), QStringLiteral("roof"));
    QCOMPARE(expense.value(1).toString(), QStringLiteral("2027-05"));
    QCOMPARE(expense.value(2).toInt(), 1);

    QSqlQuery flow{connection};
    QVERIFY(flow.exec(QStringLiteral("SELECT month, income_minor, saving_buffer_minor, "
                                     "profile_id FROM month_flow")));
    QVERIFY(flow.next());
    QCOMPARE(flow.value(0).toString(), QStringLiteral("2026-01"));
    QCOMPARE(flow.value(1).toLongLong(), qint64(3000000));
    QCOMPARE(flow.value(2).toLongLong(), qint64(400000));
    QCOMPARE(flow.value(3).toInt(), 1);

    QSqlQuery history{connection};
    QVERIFY(
        history.exec(QStringLiteral("SELECT changed_at, phase, profile_id FROM phase_history")));
    QVERIFY(history.next());
    QCOMPARE(history.value(0).toString(), QStringLiteral("2026-01-31T12:00:00Z"));
    QCOMPARE(history.value(1).toString(), QStringLiteral("building-wealth"));
    QCOMPARE(history.value(2).toInt(), 1);

    // The rebuilt keys are per household now: the same month and the same
    // second can exist under a second profile.
    QSqlQuery secondProfile{connection};
    QVERIFY(secondProfile.exec(
        QStringLiteral("INSERT INTO profile (display_name, base_currency, phase, "
                       "risk_tolerance_bp, jurisdiction_id, safe_withdrawal_bp, "
                       "capital_tax_bp, fi_multiplier_low, fi_multiplier_default, "
                       "fi_multiplier_high, created_at) VALUES ('Budget', 'SEK', "
                       "'building-wealth', 5000, 'SE', 400, 100, 21, 25, 33, "
                       "'2026-02-01T12:00:00Z')")));

    QSqlQuery secondFlow{connection};
    QVERIFY(secondFlow.exec(
        QStringLiteral("INSERT INTO month_flow (profile_id, month, income_minor, needs_minor, "
                       "wants_minor, saving_minor, currency) VALUES "
                       "(2, '2026-01', 1, 2, 3, 4, 'SEK')")));
    QSqlQuery secondHistory{connection};
    QVERIFY(secondHistory.exec(
        QStringLiteral("INSERT INTO phase_history (profile_id, changed_at, phase) VALUES "
                       "(2, '2026-01-31T12:00:00Z', 'building-wealth')")));
}

/// A version 7 file was written before examples existed, so every profile in it
/// is the household's own. Upgrading must say so rather than leave the flag
/// unreadable or guess that a household was an example.
void TestMigrator::versionSevenProfileLoadsAsNotAnExample()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    QVERIFY(migrations.size() >= std::size_t{8});
    const std::span<const sqlite::Migration> upToSeven{migrations.data(), migrations.size() - 1};

    const application::Result<int> atSeven = sqlite::applyMigrations(connection, upToSeven);
    QVERIFY2(atSeven.ok(), atSeven.error().c_str());
    QCOMPARE(atSeven.value(), 7);
    QVERIFY(!hasColumn(connection, "profile", "is_example"));

    QSqlQuery insert{connection};
    QVERIFY2(insert.exec(QString::fromUtf8(kInsertVersionSixProfile)),
             qPrintable(insert.lastError().text()));

    const application::Result<int> atEight = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atEight.ok(), atEight.error().c_str());
    QCOMPARE(atEight.value(), kExpectedVersion);

    QVERIFY(hasColumn(connection, "profile", "is_example"));
    QCOMPARE(exampleFlagOfStoredProfile(connection), 0);
    // The row itself is untouched: the flag is the only thing that arrived.
    QCOMPARE(trackingModeOfStoredProfile(connection), QStringLiteral("accounting"));
}

void TestMigrator::versionSixWithDataButNoProfileIsRefused()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    const std::span<const sqlite::Migration> upToVersionSix{migrations.data(), 6};

    const application::Result<int> atSix = sqlite::applyMigrations(connection, upToVersionSix);
    QVERIFY2(atSix.ok(), atSix.error().c_str());
    QCOMPARE(atSix.value(), 6);

    // A version 6 file could hold household rows without ever requiring the
    // single profile row. There is no owner to give them, and a NULL owner
    // would hide them from every scoped read and from the backup.
    QSqlQuery asset{connection};
    QVERIFY2(asset.exec(QStringLiteral("INSERT INTO asset (id, name, kind, bucket, liquid, "
                                       "included_in_headline) VALUES "
                                       "('fund', 'Global fund', 'fund', 'growth', 1, 1)")),
             qPrintable(asset.lastError().text()));

    const application::Result<int> atSeven = sqlite::applyMigrations(connection, migrations);
    QVERIFY(!atSeven.ok());
    QVERIFY(atSeven.error().find("no profile") != std::string::npos);

    // Refusing is not a partial upgrade: the file is still a version 6 file,
    // with the row that could not be owned.
    const application::Result<int> version = sqlite::schemaVersion(connection);
    QVERIFY2(version.ok(), version.error().c_str());
    QCOMPARE(version.value(), 6);
    QVERIFY(!hasColumn(connection, "asset", "profile_id"));

    QSqlQuery count{connection};
    QVERIFY(count.exec(QStringLiteral("SELECT count(*) FROM asset")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TestMigrator::migratingTwiceChangesNothing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const sqlite::MigrationList migrations = sqlite::schemaMigrations();
    const application::Result<int> first = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(first.ok(), first.error().c_str());
    QCOMPARE(first.value(), kExpectedVersion);

    // A row written after the first run has to survive the second one, which is
    // what "no-op" means for a file that is already up to date.
    QSqlQuery insert{connection};
    QVERIFY(
        insert.exec(QStringLiteral("INSERT INTO meta (key, value) VALUES ('written', 'once')")));

    const application::Result<int> second = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(second.ok(), second.error().c_str());
    QCOMPARE(second.value(), kExpectedVersion);

    QSqlQuery count{connection};
    QVERIFY(count.exec(QStringLiteral("SELECT count(*) FROM meta")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TestMigrator::newerSchemaIsRefused()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    QSqlQuery bump{connection};
    const QString statement = QStringLiteral("PRAGMA user_version = %1").arg(kExpectedVersion + 1);
    QVERIFY2(bump.exec(statement), qPrintable(bump.lastError().text()));

    const application::Status migrated = database.migrate();
    QVERIFY(!migrated.ok());
    QVERIFY(migrated.error().find("version " + std::to_string(kExpectedVersion + 1)) !=
            std::string::npos);
    QVERIFY(migrated.error().find("version " + std::to_string(kExpectedVersion)) !=
            std::string::npos);
    QVERIFY(migrated.error().find("newer hinkwise") != std::string::npos);

    // Refusing is not the same as downgrading: the file is left exactly as the
    // newer release wrote it, with no half-built old schema beside it.
    const application::Result<int> version = sqlite::schemaVersion(connection);
    QVERIFY2(version.ok(), version.error().c_str());
    QCOMPARE(version.value(), kExpectedVersion + 1);
    for (const char* table : kTables)
        QCOMPARE(tableCount(connection, table), 0);
}

void TestMigrator::failedMigrationLeavesTheVersionUnchanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const std::vector<sqlite::Migration> migrations{
        {1, &createProbeTable},
        {2, &createTableAndFail},
    };

    const application::Result<int> applied = sqlite::applyMigrations(connection, migrations);
    QVERIFY(!applied.ok());
    QVERIFY(applied.error().find("deliberate failure") != std::string::npos);

    const application::Result<int> version = sqlite::schemaVersion(connection);
    QVERIFY2(version.ok(), version.error().c_str());
    QCOMPARE(version.value(), 1);

    // The step that ran is on file; the step that failed left nothing behind.
    QCOMPARE(tableCount(connection, "probe"), 1);
    QCOMPARE(tableCount(connection, "half_done"), 0);
}

void TestMigrator::migrationsMustBeListedInOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto opened = sqlite::Database::open(directory.filePath(QStringLiteral("hinkwise.db")));
    QVERIFY2(opened.ok(), opened.error().c_str());
    sqlite::Database database = std::move(opened.value());
    QSqlDatabase connection = database.connection();

    const std::vector<sqlite::Migration> outOfOrder{
        {2, &createProbeTable},
        {1, &createProbeTable},
    };

    const application::Result<int> applied = sqlite::applyMigrations(connection, outOfOrder);
    QVERIFY(!applied.ok());
    QVERIFY(applied.error().find("increasing order") != std::string::npos);

    const application::Result<int> version = sqlite::schemaVersion(connection);
    QVERIFY2(version.ok(), version.error().c_str());
    QCOMPARE(version.value(), 0);
}

QTEST_GUILESS_MAIN(TestMigrator)

#include "tst_migrator.moc"
