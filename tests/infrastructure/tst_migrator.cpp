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
constexpr int kExpectedVersion = 2;

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
    QVERIFY(migrated.error().find("version 3") != std::string::npos);
    QVERIFY(migrated.error().find("version 2") != std::string::npos);
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
