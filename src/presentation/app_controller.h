// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"
#include "application/examples.h"
#include "application/result.h"
#include "application/transaction.h"
#include "domain/asset.h"
#include "domain/household.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QQmlEngine;
class QJSEngine;

namespace hinkwise::infrastructure::sqlite {
class Database;
class SqliteAssetRepository;
class SqliteDebtBalanceRepository;
class SqliteLiabilityRepository;
class SqliteMonthFlowRepository;
class SqlitePhaseHistoryRepository;
class SqlitePlannedExpenseRepository;
class SqliteProfileRepository;
class SqliteTransactionFactory;
class SqliteValuationRepository;
} // namespace hinkwise::infrastructure::sqlite

namespace hinkwise::presentation {

/// The application's single connection to the household's data.
///
/// It owns the SQLite database and one repository per port, hands the screen
/// models a snapshot, and is the only way a screen changes anything: every
/// mutation goes through one of the use cases below and announces itself with
/// dataChanged(), which is what keeps the rail footer and the dashboard in step
/// with the page that did the writing.
///
/// It is also the transaction factory, because it owns the connection the
/// boundary is drawn on. A screen that writes more than one row takes a
/// transaction from beginTransaction() and commits it once every write has
/// succeeded.
class AppController : public QObject, public application::ITransactionFactory {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit AppController(QObject* parent);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /// QML's singleton factory. It returns the instance the executable created,
    /// so there is one database connection and not one per QML engine.
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// Null until the executable has constructed its controller.
    static AppController* instance();

    /// Opens the database under QStandardPaths::AppDataLocation, migrates it
    /// and creates the repositories. Idempotent; false means the database could
    /// not be opened or migrated, and lastError() says why.
    Q_INVOKABLE bool initialise();

    /// Empty while initialise() has succeeded.
    QString lastError() const;

    /// The dashboard as of the current month. Before initialise() has
    /// succeeded it is the empty household, which is what the empty state
    /// renders.
    application::DashboardSnapshot dashboardSnapshot() const;

    /// The month every screen is about. Read once per call so that two screens
    /// refreshed in the same minute cannot disagree about which month it is.
    static domain::YearMonth currentMonth();

    /// The stored profile, std::nullopt until the household has been set up.
    std::optional<domain::Profile> profile() const;

    /// The profile every scoped repository currently reads and writes, or zero
    /// while the database holds no profile at all. Reading it also re-points
    /// the repositories if another connection changed the stored pointer.
    Q_INVOKABLE qint64 activeProfileId() const;

    /// Makes `id` the active profile: the scoped repositories are re-pointed
    /// and dataChanged() is emitted, so every screen reloads the household it
    /// now describes. Fails when no profile has that id.
    application::Status setActiveProfile(std::int64_t id);

    /// Every stored profile, oldest id first: what the settings list and the
    /// rail's switcher draw. Empty when the database is not open.
    std::vector<application::ProfileSummary> profiles() const;

    /// Creates an empty household and makes it active. It is named so the list
    /// can tell it apart from the others, and the caller sends the user to
    /// onboarding, which fills the profile the wizard reviewed.
    application::Result<domain::Profile> createEmptyProfile();

    /// Copies `sourceId` into a new household called `name` and makes it
    /// active. The copy shares no row with its source, which is what makes it
    /// independent, and it is never an example: duplicating an example is how
    /// the household takes it over, and an adopted household is its own.
    application::Result<domain::Profile> duplicateProfile(std::int64_t sourceId,
                                                          const std::string& name);

    /// Creates an example, fills it with the story's figures and makes it
    /// active, all in one transaction: an example that was half written would
    /// be a household whose numbers do not add up.
    application::Result<domain::Profile> createExampleProfile(application::ExampleProfile kind);

    /// Removes the profile and everything recorded under it, and points the
    /// repositories at the household that takes over. Refuses the last profile:
    /// a file with no household has nothing to show and nothing to save into.
    application::Status removeProfile(std::int64_t id);

    /// Saves the profile as a whole, which is how settings edits the name and
    /// the jurisdiction: the row is one value, and the use cases only read it.
    application::Status saveProfile(const domain::Profile& profile);

    /// Moves the household to `next` through the changePhase use case, which
    /// appends the history entry as well as saving. The timestamp is stamped
    /// here because the application layer never reads a clock.
    application::Status changePhase(domain::Phase next);

    /// Every phase change ever recorded, oldest first.
    std::vector<application::PhaseChange> phaseHistory() const;

    /// The database file this session has open, or the one it would open.
    QString databasePath() const;

    /// Writes the whole household to `path` as the JSON backup the
    /// infrastructure layer defines. Returns the number of records written.
    ///
    /// Every valuation and every debt balance goes in, not just the latest: a
    /// backup that dropped the history would be worth less than none. The file
    /// is written through QSaveFile, so a failure leaves no partial file behind.
    ///
    /// The database file and its SQLite sidecars are refused as destinations,
    /// because overwriting them destroys the household's only copy, and the
    /// committed backup is given owner-only permissions.
    application::Result<int> exportBackup(const QString& path) const;

    /// Everything the household holds, ordered by name. A read that fails
    /// leaves the list empty rather than failing the screen, the same way
    /// buildDashboard() degrades.
    std::vector<domain::Asset> assets() const;
    std::vector<domain::Liability> liabilities() const;

    /// The most recent valuation of an asset, and the one recorded for a
    /// particular month. std::nullopt when there is none.
    std::optional<domain::Money> latestAssetBalance(std::string_view assetId) const;
    std::optional<domain::Money> assetBalanceAt(std::string_view assetId,
                                                domain::YearMonth month) const;
    std::optional<domain::Money> latestDebtBalance(std::string_view liabilityId) const;

    /// Every valuation of an asset and every balance of a debt, oldest first.
    /// Empty when the account has never been recorded, and also when the read
    /// fails: a row the update flow cannot describe is a row it leaves alone.
    std::vector<application::MoneyPoint> assetHistory(std::string_view assetId) const;
    std::vector<application::MoneyPoint> debtHistory(std::string_view liabilityId) const;

    /// The recorded month flows, newest first, at most `count` of them. This is
    /// what the cashflow history is drawn from: a month the household never
    /// recorded is simply not in the list, which is different from a month of
    /// zeros.
    std::vector<application::MonthFlowRecord> recentFlows(int count) const;

    /// The flow recorded for one month, std::nullopt when there is none.
    std::optional<domain::MonthFlow> monthFlow(domain::YearMonth month) const;

    /// Known future costs, soonest first. Their amounts carry their own
    /// currency; the buffer target only takes the ones in the profile's.
    std::vector<domain::PlannedExpense> plannedExpenses() const;

    /// The use cases, with the repository each one needs already supplied.
    ///
    /// Every one returns the use case's own message on failure, and emits
    /// dataChanged() only when the write succeeded, so a screen can show the
    /// message and leave its state alone. Inside a transaction the signal waits
    /// for the commit, so a form that writes several rows is announced once and
    /// only when they are really stored.
    /// Creates the household. The tracking mode is part of the same write: the
    /// wizard asks for it on the phase step, and a household that started in
    /// one mode and was saved again in the other would have two writes where
    /// there is one decision.
    application::Status
    createHousehold(domain::Currency currency, domain::Phase phase,
                    domain::TrackingMode trackingMode = domain::TrackingMode::Planning);
    application::Status addAsset(const domain::Asset& asset);
    application::Status removeAsset(std::string_view assetId);
    application::Status addLiability(const domain::Liability& liability);
    application::Status updateLiability(const domain::Liability& liability);
    application::Status removeLiability(std::string_view liabilityId);
    application::Status addPlannedExpense(const domain::PlannedExpense& expense);
    application::Status removePlannedExpense(std::string_view expenseId);
    application::Status recordMonth(domain::YearMonth month, const domain::MonthFlow& flow);
    application::Status recordValuation(std::string_view assetId, domain::YearMonth month,
                                        domain::Money balance);
    application::Status recordDebtBalance(std::string_view liabilityId, domain::YearMonth month,
                                          domain::Money balance);

    /// Begins a transaction that covers every write until it is committed.
    ///
    /// A form that makes one decision out of several rows takes one of these:
    /// the wizard's household, a whole month of balances, a debt together with
    /// the balance it was created with. commit() stores them together; dropping
    /// the transaction without committing rolls all of them back, which is what
    /// a failure halfway through the writes ends up doing.
    ///
    /// dataChanged() is held back until the commit, so no screen reloads and
    /// shows rows that a rollback may still throw away; the commit announces the
    /// whole transaction once.
    ///
    /// A second begin() while one is open fails rather than nesting, and the
    /// transaction that is open is left alone.
    application::Result<std::unique_ptr<application::Transaction>> beginTransaction();

    /// The application layer's ITransactionFactory, so that a use case which
    /// writes more than one row - changePhase() - can be handed this controller.
    /// The same call as beginTransaction().
    application::Result<std::unique_ptr<application::Transaction>> begin() override
    {
        return beginTransaction();
    }

signals:
    /// The household's data changed. Every screen reloads from the controller,
    /// so the page that wrote and the rail that summarises it never disagree.
    void dataChanged();

private:
    /// The transaction beginTransaction() hands out. Defined in the
    /// implementation file because it is the one thing here that has to know
    /// when a transaction ended: it is how dataChanged() is held back and then
    /// announced once.
    class OpenTransaction;

    /// Emits dataChanged() for a write that went through, and hands the same
    /// Status back so callers do not have to branch twice.
    application::Status report(application::Status status);

    /// Called by the transaction beginTransaction() handed out, once it has
    /// ended: on a commit the whole transaction is announced in one
    /// dataChanged(), and a rollback announces nothing because nothing outside
    /// it changed.
    void endTransaction(bool committed);

    /// Reads meta.active_profile and points the scoped repositories at it.
    /// When no profile is active but some exist, the lowest id takes over; a
    /// database with no profile at all leaves them unset, which is what the
    /// wizard creates the first one into.
    application::Status activateStoredProfile() const;

    /// The controller is the one place that keeps the repository pointers in
    /// step with meta.active_profile. Every public operation calls this first,
    /// so a second connection that switched the active profile cannot leave
    /// the repositories reading one household while profile() reads another.
    application::Status syncActiveProfile() const;

    void pointScopedRepositoriesAt(std::int64_t profileId) const;

    // The database is declared first so that it outlives the repositories
    // borrowing its connection.
    std::unique_ptr<infrastructure::sqlite::Database> m_database;
    std::unique_ptr<infrastructure::sqlite::SqliteProfileRepository> m_profiles;
    std::unique_ptr<infrastructure::sqlite::SqliteAssetRepository> m_assets;
    std::unique_ptr<infrastructure::sqlite::SqliteLiabilityRepository> m_liabilities;
    std::unique_ptr<infrastructure::sqlite::SqliteDebtBalanceRepository> m_debtBalances;
    std::unique_ptr<infrastructure::sqlite::SqliteValuationRepository> m_valuations;
    std::unique_ptr<infrastructure::sqlite::SqliteMonthFlowRepository> m_flows;
    std::unique_ptr<infrastructure::sqlite::SqlitePlannedExpenseRepository> m_plannedExpenses;
    std::unique_ptr<infrastructure::sqlite::SqlitePhaseHistoryRepository> m_phaseHistory;
    std::unique_ptr<infrastructure::sqlite::SqliteTransactionFactory> m_transactions;
    QString m_lastError;

    /// The profile the scoped repositories were last pointed at. It is mutable
    /// because the const reads re-point it when another connection moves the
    /// stored pointer.
    mutable std::int64_t m_activeProfileId = 0;

    /// How many transactions this controller has open. Nesting is refused, so
    /// it is 0 or 1; it is a count so that a rollback and a commit can be told
    /// apart without the number going negative.
    int m_openTransactions = 0;

    /// A write went through while a transaction was open, so its commit has
    /// something to announce.
    bool m_changedInTransaction = false;

    static AppController* s_instance;
};

} // namespace hinkwise::presentation
