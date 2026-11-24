// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "migrator.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <string>

namespace hinkwise::infrastructure::sqlite {

namespace {

std::string toStd(const QString& text)
{
    return text.toStdString();
}

application::Status execute(QSqlDatabase& database, const char* statement)
{
    QSqlQuery query{database};
    if (!query.exec(QString::fromUtf8(statement)))
        return application::Status::err("migration: '" + std::string{statement} +
                                        "' failed: " + toStd(query.lastError().text()));
    return application::Status::success();
}

/// The schema of version 1. One CREATE TABLE per statement: the QSQLITE driver
/// executes a single statement per call.
application::Status createInitialSchema(QSqlDatabase& database)
{
    static constexpr const char* kStatements[] = {
        R"(CREATE TABLE profile (
               id INTEGER PRIMARY KEY CHECK (id = 1),
               display_name TEXT NOT NULL,
               base_currency TEXT NOT NULL,
               phase TEXT NOT NULL,
               risk_tolerance_bp INTEGER NOT NULL,
               jurisdiction_id TEXT NOT NULL,
               safe_withdrawal_bp INTEGER NOT NULL,
               capital_tax_bp INTEGER NOT NULL,
               fi_multiplier_low INTEGER NOT NULL,
               fi_multiplier_default INTEGER NOT NULL,
               fi_multiplier_high INTEGER NOT NULL,
               created_at TEXT NOT NULL
           ))",
        R"(CREATE TABLE asset (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               kind TEXT NOT NULL,
               bucket TEXT NOT NULL,
               liquid INTEGER NOT NULL,
               included_in_headline INTEGER NOT NULL,
               archived INTEGER NOT NULL DEFAULT 0
           ))",
        R"(CREATE TABLE liability (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               kind TEXT NOT NULL,
               interest_bp INTEGER NOT NULL,
               archived INTEGER NOT NULL DEFAULT 0
           ))",
        R"(CREATE TABLE planned_expense (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               due_month TEXT NOT NULL,
               currency TEXT NOT NULL
           ))",
        R"(CREATE TABLE valuation (
               asset_id TEXT NOT NULL REFERENCES asset(id) ON DELETE CASCADE,
               month TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               currency TEXT NOT NULL,
               PRIMARY KEY (asset_id, month)
           ))",
        R"(CREATE TABLE debt_balance (
               liability_id TEXT NOT NULL REFERENCES liability(id) ON DELETE CASCADE,
               month TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               currency TEXT NOT NULL,
               PRIMARY KEY (liability_id, month)
           ))",
        R"(CREATE TABLE month_flow (
               month TEXT PRIMARY KEY,
               income_minor INTEGER NOT NULL,
               needs_minor INTEGER NOT NULL,
               wants_minor INTEGER NOT NULL,
               saving_minor INTEGER NOT NULL,
               currency TEXT NOT NULL
           ))",
        R"(CREATE TABLE phase_history (
               changed_at TEXT NOT NULL,
               phase TEXT NOT NULL,
               PRIMARY KEY (changed_at)
           ))",
        R"(CREATE TABLE meta (
               key TEXT PRIMARY KEY,
               value TEXT NOT NULL
           ))",
    };

    for (const char* statement : kStatements) {
        const application::Status status = execute(database, statement);
        if (!status.ok())
            return status;
    }
    return application::Status::success();
}

/// The schema of version 2: the optional saving breakdown.
///
/// The three columns are nullable and stay NULL for a month recorded before the
/// breakdown existed, which is the repository's "not broken down". A zero would
/// claim the household put nothing in that place, which is a different and
/// possibly false statement. Version 1 is left exactly as it was written: a file
/// that has already run it must not run it again, and the version gate in
/// applyMigrations() is what keeps that true.
application::Status addSavingBreakdown(QSqlDatabase& database)
{
    static constexpr const char* kStatements[] = {
        "ALTER TABLE month_flow ADD COLUMN saving_buffer_minor INTEGER",
        "ALTER TABLE month_flow ADD COLUMN saving_invest_minor INTEGER",
        "ALTER TABLE month_flow ADD COLUMN saving_amort_minor INTEGER",
    };

    for (const char* statement : kStatements) {
        const application::Status status = execute(database, statement);
        if (!status.ok())
            return status;
    }
    return application::Status::success();
}

/// The schema of version 3: how the household keeps its record.
///
/// One ALTER TABLE with a NOT NULL default, so a file that already holds a
/// profile comes out of the migration with 'planning': the mode that expects an
/// old figure rather than calling it a gap. SQLite fills the column in for the
/// existing row, so there is no second statement to run and nothing to get
/// half done.
application::Status addTrackingMode(QSqlDatabase& database)
{
    return execute(database, "ALTER TABLE profile ADD COLUMN tracking_mode TEXT NOT NULL "
                             "DEFAULT 'planning'");
}

application::Status setSchemaVersion(QSqlDatabase& database, int version)
{
    // user_version is a header field, not a value a statement can bind, and the
    // number is an int from the migration list, never user input.
    const std::string statement = "PRAGMA user_version = " + std::to_string(version);
    return execute(database, statement.c_str());
}

std::string contextMessage(const QString& context, const QSqlError& error)
{
    return context.toStdString() + ": " + toStd(error.text());
}

} // namespace

MigrationList schemaMigrations()
{
    return MigrationList{
        {1, &createInitialSchema}, {2, &addSavingBreakdown}, {3, &addTrackingMode}};
}

application::Result<int> schemaVersion(QSqlDatabase& database)
{
    QSqlQuery query{database};
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return application::Result<int>::err(contextMessage(
            QStringLiteral("migration: cannot read user_version"), query.lastError()));
    return application::Result<int>::ok(query.value(0).toInt());
}

application::Result<int> applyMigrations(QSqlDatabase& database,
                                         std::span<const Migration> migrations)
{
    if (!database.isOpen())
        return application::Result<int>::err("migration: database connection is not open");

    int listed = 0;
    for (const Migration& migration : migrations) {
        if (migration.version <= listed)
            return application::Result<int>::err(
                "migration: version " + std::to_string(migration.version) +
                " is listed after version " + std::to_string(listed) +
                "; migrations must be listed once, in increasing order");
        listed = migration.version;
    }

    const application::Result<int> stored = schemaVersion(database);
    if (!stored.ok())
        return stored;

    int current = stored.value();
    const int latest = migrations.empty() ? 0 : migrations.back().version;
    if (current > latest)
        return application::Result<int>::err(
            "migration: the database is at schema version " + std::to_string(current) +
            ", but this build of hinkwise only knows version " + std::to_string(latest) +
            "; the database was written by a newer hinkwise");

    for (const Migration& migration : migrations) {
        if (migration.version <= current)
            continue;

        const std::string step = "migration " + std::to_string(migration.version);
        if (migration.apply == nullptr)
            return application::Result<int>::err(step + ": no apply function");

        if (!database.transaction())
            return application::Result<int>::err(contextMessage(
                QString::fromStdString(step + ": cannot begin transaction"), database.lastError()));

        const application::Status applied = migration.apply(database);
        if (!applied.ok()) {
            database.rollback();
            return application::Result<int>::err(applied.error());
        }

        const application::Status bumped = setSchemaVersion(database, migration.version);
        if (!bumped.ok()) {
            database.rollback();
            return application::Result<int>::err(bumped.error());
        }

        if (!database.commit()) {
            const std::string message = contextMessage(
                QString::fromStdString(step + ": cannot commit"), database.lastError());
            database.rollback();
            return application::Result<int>::err(message);
        }
        current = migration.version;
    }

    return application::Result<int>::ok(current);
}

} // namespace hinkwise::infrastructure::sqlite
