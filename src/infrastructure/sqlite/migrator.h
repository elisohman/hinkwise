// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/result.h"

#include <QSqlDatabase>
#include <span>
#include <vector>

namespace hinkwise::infrastructure::sqlite {

/// One schema step. `apply` runs inside the transaction the migrator opens, so
/// a step either lands completely or has no effect at all - there is no
/// half-migrated file.
///
/// The version is the value `PRAGMA user_version` carries once the step is
/// done, so the file itself records how far it has come.
struct Migration {
    int version = 0;
    application::Status (*apply)(QSqlDatabase& database) = nullptr;
};

using MigrationList = std::vector<Migration>;

/// Every migration the application knows, in version order starting at 1.
MigrationList schemaMigrations();

/// Applies the migrations the file has not seen yet and returns the version it
/// reached. Already applied steps are skipped, so calling this on every start is
/// the intended use.
///
/// Each step, including its `PRAGMA user_version` bump, is one transaction: when
/// `apply` reports an error the step is rolled back and the version stays where
/// it was, which keeps the file loadable by the previous release.
application::Result<int> applyMigrations(QSqlDatabase& database,
                                         std::span<const Migration> migrations);

/// `PRAGMA user_version`, which is 0 for a file that has never been migrated.
application::Result<int> schemaVersion(QSqlDatabase& database);

} // namespace hinkwise::infrastructure::sqlite
