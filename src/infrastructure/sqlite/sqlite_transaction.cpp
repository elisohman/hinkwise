// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sqlite_transaction.h"

#include <QSqlError>
#include <QString>
#include <memory>
#include <string>
#include <utility>

namespace hinkwise::infrastructure::sqlite {

namespace {

std::string toStd(const QString& text)
{
    return text.toStdString();
}

} // namespace

SqliteTransaction::SqliteTransaction(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

SqliteTransaction::~SqliteTransaction()
{
    if (m_finished)
        return;

    // Nobody committed, so the work ends here. The rollback itself can fail, on
    // a connection that has gone away; there is no one to report that to, and
    // SQLite discards an open transaction when the connection closes anyway.
    m_connection.rollback();
}

application::Status SqliteTransaction::commit()
{
    if (m_finished)
        return application::Status::err("transaction: it has already ended, so nothing was stored");

    m_finished = true;
    if (m_connection.commit())
        return application::Status::success();

    const std::string failure = toStd(m_connection.lastError().text());
    // SQLite keeps the transaction open when a COMMIT is refused - a deferred
    // constraint that only reports at the end, a file another process holds -
    // so the rollback has to be explicit here rather than left to the
    // destructor's next caller. Otherwise the refused rows would sit in an open
    // transaction and be committed by whatever came after.
    m_connection.rollback();
    return application::Status::err("transaction: commit failed: " + failure);
}

SqliteTransactionFactory::SqliteTransactionFactory(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::unique_ptr<application::Transaction>> SqliteTransactionFactory::begin()
{
    using Outcome = application::Result<std::unique_ptr<application::Transaction>>;

    if (!m_connection.isOpen())
        return Outcome::err("transaction: the database connection is not open");

    if (!m_connection.transaction()) {
        // The second begin() of a nested pair lands here: SQLite refuses to
        // open a transaction inside one. Joining the outer transaction
        // silently would be worse - the inner scope's rollback would take the
        // outer scope's work with it - so the refusal is reported as it is.
        const std::string failure = toStd(m_connection.lastError().text());
        return Outcome::err("transaction: cannot begin: " + failure);
    }

    return Outcome::ok(std::make_unique<SqliteTransaction>(m_connection));
}

} // namespace hinkwise::infrastructure::sqlite
