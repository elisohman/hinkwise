// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/result.h"

#include <QSqlDatabase>
#include <QString>

namespace hinkwise::infrastructure::sqlite {

/// The application's SQLite file: where it lives, how it is opened, and how it
/// is brought up to schema.
///
/// One Database owns one named QSqlDatabase connection, so several databases
/// can be open at once - the tests rely on that. Repositories borrow the
/// connection and must not outlive the Database that created it.
///
/// The file is created 0600: a household's balance sheet is not something other
/// users of the machine should be able to read.
class Database {
public:
    /// The real file, under QStandardPaths::AppDataLocation.
    static QString defaultPath();

    /// Opens `path`, creating the parent directory and the file itself. An
    /// explicit path is what makes the tests use a real file in a temporary
    /// directory instead of :memory:.
    static application::Result<Database> open(const QString& path);

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    /// Creates the schema, or upgrades it to the current version. Idempotent
    /// and safe to call on every start.
    application::Status migrate();

    /// The borrowed connection, for the repositories. Foreign keys are already
    /// enabled on it.
    QSqlDatabase connection() const;

    const QString& path() const noexcept { return m_path; }

private:
    Database(QString connectionName, QString path);

    /// Closes and forgets the connection. Called by the destructor and by a
    /// move assignment that reuses the object.
    void release();

    QString m_connectionName;
    QString m_path;
};

} // namespace hinkwise::infrastructure::sqlite
