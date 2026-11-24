// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "result.h"

#include <memory>

namespace hinkwise::application {

/// One transaction over everything the repositories write to, as the
/// application layer sees it: Qt-free, so a use case can be given a boundary
/// without knowing there is a database underneath.
///
/// The contract is the whole point of this type, so it is stated once, here:
///
/// - Everything written between begin() and commit() is stored together or not
///   at all. The destructor rolls back a transaction that was never committed,
///   so leaving the scope on any path - an early return, a write that failed,
///   an exception - undoes the work instead of leaving half of it behind.
/// - commit() can fail, and then the work is rolled back as well. A caller that
///   ignores the failure still does not end up with half a household.
/// - A Transaction is not copyable, because two owners of one boundary would
///   each believe they could end it, and it is not thread-safe: it belongs to
///   the thread that began it, which is the thread the repositories run on.
///
/// A single write needs none of this. The boundary is for the operations where
/// two rows are one decision: a household, a month of balances, a debt and the
/// balance it was created with.
class Transaction {
public:
    virtual ~Transaction() = default;

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    /// Stores everything written since the transaction began, and ends it.
    /// Fails with a message fit for a dialog; the work is rolled back before
    /// the failure is returned.
    virtual Status commit() = 0;

protected:
    Transaction() = default;
};

/// Where a transaction comes from. A use case takes one of these by reference
/// when it writes more than one row; the infrastructure layer implements it
/// over the real database, and a test implements it over its own.
///
/// The factory must outlive every transaction it handed out.
class ITransactionFactory {
public:
    virtual ~ITransactionFactory() = default;

    ITransactionFactory(const ITransactionFactory&) = delete;
    ITransactionFactory& operator=(const ITransactionFactory&) = delete;

    /// Opens a boundary. Fails with a message when none can be opened - the
    /// connection is gone, or the implementation refuses to nest.
    virtual Result<std::unique_ptr<Transaction>> begin() = 0;

protected:
    ITransactionFactory() = default;
};

} // namespace hinkwise::application
