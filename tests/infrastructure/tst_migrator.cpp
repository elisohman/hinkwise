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
constexpr int kExpectedVersion = 5;

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

    const application::Result<int> atFour = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atFour.ok(), atFour.error().c_str());
    QCOMPARE(atFour.value(), kExpectedVersion);
    for (const char* column : kLoanTermsColumns)
        QVERIFY(hasColumn(connection, "liability", column));

    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT name, interest_bp, repayment_style, "
                                       "regular_payment_minor, monthly_fee_minor "
                                       "FROM liability WHERE id = 'bolan'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Mortgage"));
    QCOMPARE(select.value(1).toInt(), 420);
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

    const application::Result<int> atFive = sqlite::applyMigrations(connection, migrations);
    QVERIFY2(atFive.ok(), atFive.error().c_str());
    QCOMPARE(atFive.value(), kExpectedVersion);
    QVERIFY(hasColumn(connection, "liability", "term_months"));

    QSqlQuery select{connection};
    QVERIFY(select.exec(QStringLiteral("SELECT name, interest_bp, repayment_style, "
                                       "regular_payment_minor, monthly_fee_minor, term_months "
                                       "FROM liability WHERE id = 'csn'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Student loan"));
    QCOMPARE(select.value(1).toInt(), 150);
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
