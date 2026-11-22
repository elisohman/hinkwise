// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "database.h"

#include "migrator.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <atomic>
#include <string>
#include <utility>

namespace hinkwise::infrastructure::sqlite {

namespace {

/// Owner read and write only. Every other bit is off, including group and
/// other, which is the point.
constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

/// Connection names must be unique per open database, because QSqlDatabase is a
/// global registry keyed by name.
QString nextConnectionName()
{
    static std::atomic<unsigned long long> counter{0};
    const unsigned long long id = counter.fetch_add(1);
    return QStringLiteral("hinkwise-%1").arg(id);
}

std::string toStd(const QString& text)
{
    return text.toStdString();
}

} // namespace

QString Database::defaultPath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory + QStringLiteral("/hinkwise.db");
}

Database::Database(QString connectionName, QString path)
    : m_connectionName(std::move(connectionName))
    , m_path(std::move(path))
{
}

Database::Database(Database&& other) noexcept
    : m_connectionName(std::move(other.m_connectionName))
    , m_path(std::move(other.m_path))
{
    other.m_connectionName.clear();
    other.m_path.clear();
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        release();
        m_connectionName = std::move(other.m_connectionName);
        m_path = std::move(other.m_path);
        other.m_connectionName.clear();
        other.m_path.clear();
    }
    return *this;
}

Database::~Database()
{
    release();
}

void Database::release()
{
    if (m_connectionName.isEmpty())
        return;

    {
        QSqlDatabase connection = QSqlDatabase::database(m_connectionName, false);
        if (connection.isValid())
            connection.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
}

application::Result<Database> Database::open(const QString& path)
{
    if (path.isEmpty())
        return application::Result<Database>::err("database: no file path given");

    const QString directory = QFileInfo{path}.absolutePath();
    if (!QDir{}.mkpath(directory))
        return application::Result<Database>::err("database: cannot create directory '" +
                                                  toStd(path) + "'");

    // The file is created before SQLite sees it, so the permissions are in place
    // before a single byte of the household's data is written. NewOnly makes
    // creation atomic: an existence check followed by a truncating open lets two
    // processes both decide they made the file, and the second one wipes the
    // first one's fresh database. If the file is already there, open it without
    // truncation and let SQLite read what it holds.
    QFile file{path};
    if (!file.open(QIODevice::NewOnly)) {
        const QString createError = file.errorString();
        file.unsetError();
        if (!QFile::exists(path))
            return application::Result<Database>::err("database: cannot create file '" +
                                                      toStd(path) + "': " + toStd(createError));
        if (!file.open(QIODevice::ReadWrite))
            return application::Result<Database>::err("database: cannot open file '" + toStd(path) +
                                                      "': " + toStd(file.errorString()));
    }
    file.close();

    if (!QFile::setPermissions(path, kOwnerOnly))
        return application::Result<Database>::err(
            "database: cannot set owner-only permissions on '" + toStd(path) + "'");

    const QString connectionName = nextConnectionName();
    {
        QSqlDatabase connection =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        connection.setDatabaseName(path);

        if (!connection.open()) {
            const std::string message = toStd(connection.lastError().text());
            connection = QSqlDatabase{};
            QSqlDatabase::removeDatabase(connectionName);
            return application::Result<Database>::err("database: cannot open '" + toStd(path) +
                                                      "': " + message);
        }

        // Per connection, not per file, so it has to be set on every open. The
        // asset and liability cascades are the only thing keeping valuation and
        // debt rows from outliving the holding they belong to.
        QSqlQuery query{connection};
        if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
            !query.exec(QStringLiteral("PRAGMA foreign_keys")) || !query.next() ||
            query.value(0).toInt() != 1) {
            const std::string message = toStd(query.lastError().text());
            query = QSqlQuery{};
            connection.close();
            connection = QSqlDatabase{};
            QSqlDatabase::removeDatabase(connectionName);
            return application::Result<Database>::err(
                "database: SQLite without foreign key enforcement in '" + toStd(path) + "'" +
                (message.empty() ? std::string{} : ": " + message));
        }
    }

    return application::Result<Database>::ok(Database{connectionName, path});
}

application::Status Database::migrate()
{
    if (m_connectionName.isEmpty())
        return application::Status::err("database: migrate called on a closed database");

    QSqlDatabase connection = QSqlDatabase::database(m_connectionName, false);
    if (!connection.isOpen())
        return application::Status::err("database: connection is not open");

    const application::Result<int> version = applyMigrations(connection, schemaMigrations());
    if (!version.ok())
        return application::Status::err(version.error());
    return application::Status::success();
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

} // namespace hinkwise::infrastructure::sqlite
