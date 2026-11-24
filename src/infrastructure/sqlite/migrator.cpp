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

/// The schema of version 4: each loan's own repayment terms.
///
/// Every column has a NOT NULL default, so a file that already holds a loan
/// comes out of the migration as fixed amortisation with a zero payment. That
/// is the model's "no terms recorded yet": the plan keeps the balance flat and
/// says so instead of inventing a paydown the household never entered. A
/// monthly fee belongs to an annuity, so its default of zero is the only
/// meaningful value for an upgraded row.
application::Status addLoanTerms(QSqlDatabase& database)
{
    static constexpr const char* kStatements[] = {
        "ALTER TABLE liability ADD COLUMN repayment_style TEXT NOT NULL DEFAULT "
        "'fixed-amortisation'",
        "ALTER TABLE liability ADD COLUMN regular_payment_minor INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE liability ADD COLUMN monthly_fee_minor INTEGER NOT NULL DEFAULT 0",
    };

    for (const char* statement : kStatements) {
        const application::Status status = execute(database, statement);
        if (!status.ok())
            return status;
    }
    return application::Status::success();
}

/// The schema of version 5: how long a loan still has to run.
///
/// One ALTER TABLE with a NOT NULL default, so a file that already holds a loan
/// comes out of the migration saying it states no term. That is not the same as
/// a loan with no time left: with no term the plan keeps using the payment the
/// household recorded, exactly as before, and an annuity that states a term is
/// the one that gets recalculated each January.
application::Status addLoanTerm(QSqlDatabase& database)
{
    return execute(database,
                   "ALTER TABLE liability ADD COLUMN term_months INTEGER NOT NULL DEFAULT 0");
}

/// The schema of version 6: a loan's interest rate to three decimals.
///
/// The rate moves from basis points (0.01 %) to parts per 100 000 (0.001 %),
/// which is the precision a lender quotes. The old column is copied across by
/// multiplying by ten, so a recorded rate is widened and never lost, and only
/// then dropped. Every statement runs inside the transaction the migrator
/// wraps around a step, so a failure leaves the old column and its values in
/// place rather than a half-converted row.
application::Status addThreeDecimalInterestRate(QSqlDatabase& database)
{
    static constexpr const char* kStatements[] = {
        "ALTER TABLE liability ADD COLUMN interest_100k INTEGER NOT NULL DEFAULT 0",
        "UPDATE liability SET interest_100k = interest_bp * 10",
        "ALTER TABLE liability DROP COLUMN interest_bp",
    };

    for (const char* statement : kStatements) {
        const application::Status status = execute(database, statement);
        if (!status.ok())
            return status;
    }
    return application::Status::success();
}

/// True when `table` holds at least one row. The table name is a literal from
/// the migration below and never comes from stored data, so it cannot become
/// SQL.
application::Result<bool> tableHasRows(QSqlDatabase& database, const char* table)
{
    QSqlQuery query{database};
    const QString statement =
        QStringLiteral("SELECT EXISTS(SELECT 1 FROM %1)").arg(QString::fromLatin1(table));
    if (!query.exec(statement) || !query.next())
        return application::Result<bool>::err("migration: cannot read " + std::string{table} +
                                              ": " + toStd(query.lastError().text()));
    return application::Result<bool>::ok(query.value(0).toInt() != 0);
}

application::Result<int> profileRowCount(QSqlDatabase& database)
{
    QSqlQuery query{database};
    if (!query.exec(QStringLiteral("SELECT count(*) FROM profile")) || !query.next())
        return application::Result<int>::err("migration: cannot count profiles: " +
                                             toStd(query.lastError().text()));
    return application::Result<int>::ok(query.value(0).toInt());
}

/// A version 6 file may have household rows without a profile row: the old
/// schema never required the single profile to exist. Those rows cannot be
/// given an owner, and writing NULL would hide them from every scoped read and
/// from the backup. The migration refuses instead, and the transaction the
/// migrator wraps around it leaves the file at version 6.
application::Status refuseOrphanedHouseholdRows(QSqlDatabase& database)
{
    const application::Result<int> profiles = profileRowCount(database);
    if (!profiles.ok())
        return application::Status::err(profiles.error());
    if (profiles.value() > 0)
        return application::Status::success();

    static constexpr const char* kHouseholdTables[] = {"asset", "liability", "planned_expense",
                                                       "month_flow", "phase_history"};
    for (const char* table : kHouseholdTables) {
        const application::Result<bool> rows = tableHasRows(database, table);
        if (!rows.ok())
            return application::Status::err(rows.error());
        if (rows.value())
            return application::Status::err(
                "migration 7: this file holds household data but no profile, so the rows "
                "cannot be given an owner; refusing to migrate");
    }
    return application::Status::success();
}

/// The schema of version 7: several households in one file.
///
/// The profile loses its single-row CHECK and every table that describes a
/// household gains a profile_id that is NOT NULL and cascades from profile. The
/// rows that are already there all belong to the one household the file was
/// written for, so the backfill is that row's id and nothing else changes about
/// the data: a version 6 file opens as the same household, now numbered 1.
///
/// SQLite will not add a REFERENCES column with a non-NULL default while
/// foreign keys are enforced, and a migration step cannot turn them off because
/// the migrator has already opened its transaction. Every table whose owner is
/// part of its shape is therefore rebuilt: a new table with the NOT NULL owner,
/// the old rows copied into it, and the old table dropped and renamed. The
/// tables that reference a rebuilt table (valuation and debt_balance) are
/// rebuilt in the same step so that dropping the old parent cannot cascade them
/// away; their schema is unchanged.
///
/// valuation and debt_balance deliberately gain no profile_id of their own.
/// They are keyed by an asset or liability id, and those ids stay globally
/// unique across profiles, so the foreign key chain already scopes every
/// valuation and balance to its holding's household. Adding profile_id there
/// would duplicate that fact and let the two copies disagree.
application::Status addProfiles(QSqlDatabase& database)
{
    const application::Status owned = refuseOrphanedHouseholdRows(database);
    if (!owned.ok())
        return owned;

    static constexpr const char* kStatements[] = {
        // The profile itself: same columns, same existing row and id, but room
        // for more than one. The old table is dropped only after the new one is
        // filled, so a failure inside this step rolls back to the single
        // profile rather than losing it.
        R"(CREATE TABLE profile_new (
               id INTEGER PRIMARY KEY AUTOINCREMENT,
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
               created_at TEXT NOT NULL,
               tracking_mode TEXT NOT NULL DEFAULT 'planning'
           ))",
        R"(INSERT INTO profile_new (id, display_name, base_currency, phase, risk_tolerance_bp,
                                    jurisdiction_id, safe_withdrawal_bp, capital_tax_bp,
                                    fi_multiplier_low, fi_multiplier_default,
                                    fi_multiplier_high, created_at, tracking_mode)
           SELECT id, display_name, base_currency, phase, risk_tolerance_bp,
                  jurisdiction_id, safe_withdrawal_bp, capital_tax_bp,
                  fi_multiplier_low, fi_multiplier_default, fi_multiplier_high,
                  created_at, tracking_mode
           FROM profile)",
        "DROP TABLE profile",
        "ALTER TABLE profile_new RENAME TO profile",

        // asset keeps its globally unique id, but its owner becomes a required
        // part of the row. valuation is rebuilt alongside it so the drop of
        // the old asset table cannot cascade the history away before it has
        // been copied.
        R"(CREATE TABLE asset_new (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               kind TEXT NOT NULL,
               bucket TEXT NOT NULL,
               liquid INTEGER NOT NULL,
               included_in_headline INTEGER NOT NULL,
               archived INTEGER NOT NULL DEFAULT 0,
               profile_id INTEGER NOT NULL REFERENCES profile(id) ON DELETE CASCADE
           ))",
        R"(INSERT INTO asset_new (id, name, kind, bucket, liquid, included_in_headline, archived,
                                  profile_id)
           SELECT id, name, kind, bucket, liquid, included_in_headline, archived,
                  (SELECT id FROM profile ORDER BY id LIMIT 1)
           FROM asset)",
        R"(CREATE TABLE valuation_new (
               asset_id TEXT NOT NULL REFERENCES asset_new(id) ON DELETE CASCADE,
               month TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               currency TEXT NOT NULL,
               PRIMARY KEY (asset_id, month)
           ))",
        R"(INSERT INTO valuation_new (asset_id, month, amount_minor, currency)
           SELECT asset_id, month, amount_minor, currency FROM valuation)",
        "DROP TABLE valuation",
        "DROP TABLE asset",
        "ALTER TABLE asset_new RENAME TO asset",
        "ALTER TABLE valuation_new RENAME TO valuation",

        // liability is rebuilt the same way, with debt_balance following it.
        R"(CREATE TABLE liability_new (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               kind TEXT NOT NULL,
               interest_100k INTEGER NOT NULL,
               repayment_style TEXT NOT NULL DEFAULT 'fixed-amortisation',
               regular_payment_minor INTEGER NOT NULL DEFAULT 0,
               monthly_fee_minor INTEGER NOT NULL DEFAULT 0,
               term_months INTEGER NOT NULL DEFAULT 0,
               archived INTEGER NOT NULL DEFAULT 0,
               profile_id INTEGER NOT NULL REFERENCES profile(id) ON DELETE CASCADE
           ))",
        R"(INSERT INTO liability_new (id, name, kind, interest_100k, repayment_style,
                                      regular_payment_minor, monthly_fee_minor, term_months,
                                      archived, profile_id)
           SELECT id, name, kind, interest_100k, repayment_style, regular_payment_minor,
                  monthly_fee_minor, term_months, archived,
                  (SELECT id FROM profile ORDER BY id LIMIT 1)
           FROM liability)",
        R"(CREATE TABLE debt_balance_new (
               liability_id TEXT NOT NULL REFERENCES liability_new(id) ON DELETE CASCADE,
               month TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               currency TEXT NOT NULL,
               PRIMARY KEY (liability_id, month)
           ))",
        R"(INSERT INTO debt_balance_new (liability_id, month, amount_minor, currency)
           SELECT liability_id, month, amount_minor, currency FROM debt_balance)",
        "DROP TABLE debt_balance",
        "DROP TABLE liability",
        "ALTER TABLE liability_new RENAME TO liability",
        "ALTER TABLE debt_balance_new RENAME TO debt_balance",

        // planned_expense has no child, so it is a plain rebuild.
        R"(CREATE TABLE planned_expense_new (
               id TEXT PRIMARY KEY,
               name TEXT NOT NULL,
               amount_minor INTEGER NOT NULL,
               due_month TEXT NOT NULL,
               currency TEXT NOT NULL,
               profile_id INTEGER NOT NULL REFERENCES profile(id) ON DELETE CASCADE
           ))",
        R"(INSERT INTO planned_expense_new (id, name, amount_minor, due_month, currency, profile_id)
           SELECT id, name, amount_minor, due_month, currency,
                  (SELECT id FROM profile ORDER BY id LIMIT 1)
           FROM planned_expense)",
        "DROP TABLE planned_expense",
        "ALTER TABLE planned_expense_new RENAME TO planned_expense",

        // month_flow's key was the month alone; the same month can now exist
        // once per household, so the table is rebuilt around the pair.
        R"(CREATE TABLE month_flow_new (
               profile_id INTEGER NOT NULL REFERENCES profile(id) ON DELETE CASCADE,
               month TEXT NOT NULL,
               income_minor INTEGER NOT NULL,
               needs_minor INTEGER NOT NULL,
               wants_minor INTEGER NOT NULL,
               saving_minor INTEGER NOT NULL,
               currency TEXT NOT NULL,
               saving_buffer_minor INTEGER,
               saving_invest_minor INTEGER,
               saving_amort_minor INTEGER,
               PRIMARY KEY (profile_id, month)
           ))",
        R"(INSERT INTO month_flow_new (profile_id, month, income_minor, needs_minor, wants_minor,
                                       saving_minor, currency, saving_buffer_minor,
                                       saving_invest_minor, saving_amort_minor)
           SELECT (SELECT id FROM profile ORDER BY id LIMIT 1), month, income_minor, needs_minor,
                  wants_minor, saving_minor, currency, saving_buffer_minor, saving_invest_minor,
                  saving_amort_minor
           FROM month_flow)",
        "DROP TABLE month_flow",
        "ALTER TABLE month_flow_new RENAME TO month_flow",

        // phase_history's key was the timestamp; two households can change
        // phase in the same second, so the key is the pair.
        R"(CREATE TABLE phase_history_new (
               profile_id INTEGER NOT NULL REFERENCES profile(id) ON DELETE CASCADE,
               changed_at TEXT NOT NULL,
               phase TEXT NOT NULL,
               PRIMARY KEY (profile_id, changed_at)
           ))",
        R"(INSERT INTO phase_history_new (profile_id, changed_at, phase)
           SELECT (SELECT id FROM profile ORDER BY id LIMIT 1), changed_at, phase
           FROM phase_history)",
        "DROP TABLE phase_history",
        "ALTER TABLE phase_history_new RENAME TO phase_history",

        // Every scoped read filters on the owner, so each owner column gets an
        // index for the same reason any foreign key does.
        "CREATE INDEX asset_profile_id ON asset (profile_id)",
        "CREATE INDEX liability_profile_id ON liability (profile_id)",
        "CREATE INDEX planned_expense_profile_id ON planned_expense (profile_id)",
        "CREATE INDEX month_flow_profile_id ON month_flow (profile_id)",
        "CREATE INDEX phase_history_profile_id ON phase_history (profile_id)",

        // The active profile is a pointer, not a second copy of the row: it
        // names which household the scoped repositories read. A file with no
        // profile at all has no pointer, and the next profile created becomes
        // active.
        R"(INSERT OR REPLACE INTO meta (key, value)
           SELECT 'active_profile', CAST(id AS TEXT) FROM profile ORDER BY id LIMIT 1)",
    };

    for (const char* statement : kStatements) {
        const application::Status status = execute(database, statement);
        if (!status.ok())
            return status;
    }
    return application::Status::success();
}

/// The schema of version 8: which profiles are examples.
///
/// A NOT NULL default of 0 means a file written before the flag existed loads
/// as a household's own record, which is what every profile in it was: the
/// examples are something the application adds later, and an old file has none.
application::Status addExampleFlag(QSqlDatabase& database)
{
    return execute(database,
                   "ALTER TABLE profile ADD COLUMN is_example INTEGER NOT NULL DEFAULT 0");
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
    return MigrationList{{1, &createInitialSchema}, {2, &addSavingBreakdown},
                         {3, &addTrackingMode},     {4, &addLoanTerms},
                         {5, &addLoanTerm},         {6, &addThreeDecimalInterestRate},
                         {7, &addProfiles},         {8, &addExampleFlag}};
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
