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
#include <string>
#include <utility>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::infrastructure;

namespace {

/// The version a fresh file reaches, which is also the number the application
/// has to keep in step with schemaMigrations().
constexpr int kExpectedVersion = 1;

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
    QVERIFY(migrated.error().find("version 2") != std::string::npos);
    QVERIFY(migrated.error().find("version 1") != std::string::npos);
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
