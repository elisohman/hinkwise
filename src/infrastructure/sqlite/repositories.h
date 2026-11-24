// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/ports.h"
#include "application/result.h"

#include <QSqlDatabase>
#include <cstdint>
#include <vector>

namespace hinkwise::infrastructure::sqlite {

/// The Qt Sql implementations of the application's ports.
///
/// Each repository borrows one connection and must not outlive the Database
/// that owns it. Every statement is prepared with bound values - no SQL is ever
/// built by concatenation - and every failure, including text that no longer
/// matches a domain enum, comes back as a Result message instead of an
/// exception.

class SqliteProfileRepository final : public application::IProfileRepository {
public:
    explicit SqliteProfileRepository(QSqlDatabase connection);

    application::Result<std::optional<domain::Profile>> load() const override;
    application::Result<std::optional<domain::Profile>> loadById(std::int64_t id) const override;
    application::Result<std::vector<application::ProfileSummary>> list() const override;
    application::Status save(const domain::Profile& profile) override;
    application::Result<domain::Profile> create(const domain::Profile& profile) override;
    application::Status remove(std::int64_t id) override;
    application::Status setActive(std::int64_t id) override;

private:
    QSqlDatabase m_connection;
};

/// The scoped repositories share one rule: before an active profile has been
/// set every read and write fails with "no active profile" rather than running
/// unscoped. An empty result would look like a household that lost its data.
class SqliteAssetRepository final : public application::IAssetRepository {
public:
    explicit SqliteAssetRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::vector<domain::Asset>> all() const override;
    application::Status upsert(const domain::Asset& asset) override;
    application::Status remove(std::string_view id) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqliteLiabilityRepository final : public application::ILiabilityRepository {
public:
    explicit SqliteLiabilityRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::vector<domain::Liability>> all() const override;
    application::Status upsert(const domain::Liability& liability) override;
    application::Status remove(std::string_view id) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqlitePlannedExpenseRepository final : public application::IPlannedExpenseRepository {
public:
    explicit SqlitePlannedExpenseRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::vector<domain::PlannedExpense>> all() const override;
    application::Status upsert(const domain::PlannedExpense& expense) override;
    application::Status remove(std::string_view id) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqliteValuationRepository final : public application::IValuationRepository {
public:
    explicit SqliteValuationRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::optional<domain::Money>>
    balanceAt(std::string_view assetId, domain::YearMonth month) const override;
    application::Result<std::optional<domain::Money>>
    latest(std::string_view assetId) const override;
    application::Result<std::vector<application::MoneyPoint>>
    history(std::string_view assetId) const override;
    application::Result<std::optional<domain::YearMonth>> mostRecentMonth() const override;
    application::Status upsert(std::string_view assetId, domain::YearMonth month,
                               domain::Money balance) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqliteDebtBalanceRepository final : public application::IDebtBalanceRepository {
public:
    explicit SqliteDebtBalanceRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::optional<domain::Money>>
    balanceAt(std::string_view liabilityId, domain::YearMonth month) const override;
    application::Result<std::optional<domain::Money>>
    latest(std::string_view liabilityId) const override;
    application::Result<std::vector<application::MoneyPoint>>
    history(std::string_view liabilityId) const override;
    application::Result<std::optional<domain::YearMonth>> mostRecentMonth() const override;
    application::Status upsert(std::string_view liabilityId, domain::YearMonth month,
                               domain::Money balance) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqliteMonthFlowRepository final : public application::IMonthFlowRepository {
public:
    explicit SqliteMonthFlowRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::optional<domain::MonthFlow>>
    at(domain::YearMonth month) const override;
    application::Result<std::vector<application::MonthFlowRecord>> recent(int count) const override;
    application::Status upsert(domain::YearMonth month, const domain::MonthFlow& flow) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

class SqlitePhaseHistoryRepository final : public application::IPhaseHistoryRepository {
public:
    explicit SqlitePhaseHistoryRepository(QSqlDatabase connection);

    void setActiveProfile(std::int64_t profileId) { m_activeProfileId = profileId; }

    application::Result<std::vector<application::PhaseChange>> all() const override;
    application::Status append(const application::PhaseChange& change) override;

private:
    QSqlDatabase m_connection;
    std::int64_t m_activeProfileId = 0;
};

} // namespace hinkwise::infrastructure::sqlite
