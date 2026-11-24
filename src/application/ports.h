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

#include <cstdint>
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

/// One amount at one month: a valuation of an asset or a balance of a debt.
/// The month was the key the amount was stored under, so the point carries it.
/// history() returns these oldest first.
struct MoneyPoint {
    domain::YearMonth month;
    domain::Money amount;
};

/// One profile as a list shows it: enough to name the household and tell which
/// way it keeps its record, without loading the whole row.
struct ProfileSummary {
    std::int64_t id = 0;
    std::string displayName;
    std::string baseCurrency;
    std::string trackingMode;
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

    /// The ACTIVE profile, std::nullopt when no profile is active yet. An
    /// empty database is not an error; a meta row pointing at a profile that
    /// does not exist is.
    virtual Result<std::optional<domain::Profile>> load() const = 0;

    /// One profile by id, std::nullopt when no row has that id. A backup needs
    /// every household, not only the active one.
    virtual Result<std::optional<domain::Profile>> loadById(std::int64_t id) const = 0;

    /// Every stored profile, oldest id first.
    virtual Result<std::vector<ProfileSummary>> list() const = 0;

    /// Saves the profile the caller is editing. A profile with an id updates
    /// that row; a profile without one updates the active row, or creates the
    /// first profile when the database holds none and makes it active.
    virtual Status save(const domain::Profile& profile) = 0;

    /// Stores `profile` as a new household, makes it active, and returns it
    /// with the id it was given.
    virtual Result<domain::Profile> create(const domain::Profile& profile) = 0;

    /// Removes the profile and everything recorded under it. Refuses to remove
    /// the last profile, because a database with no household has nothing to
    /// show and nothing to save into.
    virtual Status remove(std::int64_t id) = 0;

    /// Records which profile every scoped repository should read and write.
    /// Fails when no profile has that id.
    virtual Status setActive(std::int64_t id) = 0;
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

    /// Every valuation ever recorded for the asset, oldest first. An asset that
    /// has never been valued has no points rather than an error. This is what a
    /// backup is written from: latest() alone would lose the history.
    virtual Result<std::vector<MoneyPoint>> history(std::string_view assetId) const = 0;

    /// The latest month any asset was valued in, whichever asset it belongs to.
    /// std::nullopt means no valuation has ever been recorded, which is not an
    /// error; a failed read is one, because a household that has recorded
    /// valuations must not be told it has recorded none.
    virtual Result<std::optional<domain::YearMonth>> mostRecentMonth() const = 0;

    virtual Status upsert(std::string_view assetId, domain::YearMonth month,
                          domain::Money balance) = 0;
};

class IDebtBalanceRepository {
public:
    virtual ~IDebtBalanceRepository() = default;

    virtual Result<std::optional<domain::Money>> balanceAt(std::string_view liabilityId,
                                                           domain::YearMonth month) const = 0;

    virtual Result<std::optional<domain::Money>> latest(std::string_view liabilityId) const = 0;

    /// Every balance ever recorded for the debt, oldest first. A debt with no
    /// recorded balance has no points rather than an error.
    virtual Result<std::vector<MoneyPoint>> history(std::string_view liabilityId) const = 0;

    /// The latest month any debt balance was recorded in, whichever debt it
    /// belongs to. std::nullopt means nothing has ever been recorded.
    virtual Result<std::optional<domain::YearMonth>> mostRecentMonth() const = 0;

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
