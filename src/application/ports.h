// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/asset.h"
#include "domain/household.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hinkwise::application {

/// One month's flow together with its month, because domain::MonthFlow carries
/// no date of its own: the month was the key it was stored under.
struct MonthFlowRecord {
    domain::YearMonth month;
    domain::MonthFlow flow;
};

/// One entry in the phase history. `changedAt` is an ISO-8601 UTC timestamp
/// ("2026-01-31T12:00:00Z"). The clock lives outside the application layer, so
/// the caller stamps the change instead of the repository reading the time.
struct PhaseChange {
    std::string changedAt;
    domain::Phase phase;
};

/// The repositories are the application layer's view of storage: pure virtual,
/// no Qt types, and every failure reported through Result. The infrastructure
/// layer implements them; tests and future in-memory doubles implement the same
/// signatures.
///
/// Identifiers are std::string_view because they are only read once, within the
/// call, and an implementation copies what it needs to keep.

class IProfileRepository {
public:
    virtual ~IProfileRepository() = default;

    /// The stored profile, std::nullopt when the household has not been set up
    /// yet. An empty database is not an error.
    virtual Result<std::optional<domain::Profile>> load() const = 0;

    /// Creates or replaces the single stored profile.
    virtual Status save(const domain::Profile& profile) = 0;
};

class IAssetRepository {
public:
    virtual ~IAssetRepository() = default;

    /// Every asset the household still holds, ordered by name.
    virtual Result<std::vector<domain::Asset>> all() const = 0;

    /// Inserts the asset, or replaces the row with the same id.
    virtual Status upsert(const domain::Asset& asset) = 0;

    /// Deletes the asset and everything recorded about it. Deleting an id that
    /// is already gone is not an error.
    virtual Status remove(std::string_view id) = 0;
};

class ILiabilityRepository {
public:
    virtual ~ILiabilityRepository() = default;

    virtual Result<std::vector<domain::Liability>> all() const = 0;

    virtual Status upsert(const domain::Liability& liability) = 0;

    virtual Status remove(std::string_view id) = 0;
};

class IPlannedExpenseRepository {
public:
    virtual ~IPlannedExpenseRepository() = default;

    /// Ordered by due month, so the soonest cost comes first.
    virtual Result<std::vector<domain::PlannedExpense>> all() const = 0;

    virtual Status upsert(const domain::PlannedExpense& expense) = 0;

    virtual Status remove(std::string_view id) = 0;
};

class IValuationRepository {
public:
    virtual ~IValuationRepository() = default;

    /// std::nullopt when the asset has no valuation in that month.
    virtual Result<std::optional<domain::Money>> balanceAt(std::string_view assetId,
                                                           domain::YearMonth month) const = 0;

    /// The most recent valuation of the asset, whatever month it belongs to.
    /// std::nullopt when the asset has never been valued.
    virtual Result<std::optional<domain::Money>> latest(std::string_view assetId) const = 0;

    virtual Status upsert(std::string_view assetId, domain::YearMonth month,
                          domain::Money balance) = 0;
};

class IDebtBalanceRepository {
public:
    virtual ~IDebtBalanceRepository() = default;

    virtual Result<std::optional<domain::Money>> balanceAt(std::string_view liabilityId,
                                                           domain::YearMonth month) const = 0;

    virtual Result<std::optional<domain::Money>> latest(std::string_view liabilityId) const = 0;

    virtual Status upsert(std::string_view liabilityId, domain::YearMonth month,
                          domain::Money balance) = 0;
};

class IMonthFlowRepository {
public:
    virtual ~IMonthFlowRepository() = default;

    virtual Result<std::optional<domain::MonthFlow>> at(domain::YearMonth month) const = 0;

    /// The most recent `count` recorded months, newest first.
    virtual Result<std::vector<MonthFlowRecord>> recent(int count) const = 0;

    virtual Status upsert(domain::YearMonth month, const domain::MonthFlow& flow) = 0;
};

class IPhaseHistoryRepository {
public:
    virtual ~IPhaseHistoryRepository() = default;

    /// Oldest change first.
    virtual Result<std::vector<PhaseChange>> all() const = 0;

    virtual Status append(const PhaseChange& change) = 0;
};

} // namespace hinkwise::application
