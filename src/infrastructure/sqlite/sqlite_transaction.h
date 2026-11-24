// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/transaction.h"

#include <QSqlDatabase>
#include <memory>

namespace hinkwise::infrastructure::sqlite {

/// A transaction over one QSqlDatabase connection.
///
/// It borrows the connection the repositories write on and must not outlive the
/// Database that owns it. Statements the repositories prepare on the same
/// connection take part in the transaction, which is what makes several
/// repository calls one atomic write.
///
/// Nesting is refused, not supported: there are no savepoints here. SQLite has
/// no transaction inside a transaction, and a savepoint would let an inner
/// failure be swallowed by an outer commit - exactly the half-written household
/// this exists to prevent. begin() while one is open therefore fails, and the
/// outer transaction is left untouched so the mistake shows up as an error
/// instead of as data.
class SqliteTransaction final : public application::Transaction {
public:
    explicit SqliteTransaction(QSqlDatabase connection);
    ~SqliteTransaction() override;

    /// COMMIT. On failure the transaction is rolled back before the error is
    /// returned: a commit that did not happen must not leave its rows behind.
    application::Status commit() override;

private:
    QSqlDatabase m_connection;
    bool m_finished = false;
};

class SqliteTransactionFactory final : public application::ITransactionFactory {
public:
    explicit SqliteTransactionFactory(QSqlDatabase connection);

    application::Result<std::unique_ptr<application::Transaction>> begin() override;

private:
    QSqlDatabase m_connection;
};

} // namespace hinkwise::infrastructure::sqlite
