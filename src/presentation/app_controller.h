// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"
#include "application/result.h"
#include "domain/asset.h"
#include "domain/household.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/year_month.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
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
class SqliteProfileRepository;
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
class AppController : public QObject {
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

    /// The use cases, with the repository each one needs already supplied.
    ///
    /// Every one returns the use case's own message on failure, and emits
    /// dataChanged() only when the write succeeded, so a screen can show the
    /// message and leave its state alone.
    application::Status createHousehold(domain::Currency currency, domain::Phase phase);
    application::Status addAsset(const domain::Asset& asset);
    application::Status removeAsset(std::string_view assetId);
    application::Status addLiability(const domain::Liability& liability);
    application::Status removeLiability(std::string_view liabilityId);
    application::Status recordMonth(domain::YearMonth month, const domain::MonthFlow& flow);
    application::Status recordValuation(std::string_view assetId, domain::YearMonth month,
                                        domain::Money balance);
    application::Status recordDebtBalance(std::string_view liabilityId, domain::YearMonth month,
                                          domain::Money balance);

signals:
    /// The household's data changed. Every screen reloads from the controller,
    /// so the page that wrote and the rail that summarises it never disagree.
    void dataChanged();

private:
    /// Emits dataChanged() for a write that went through, and hands the same
    /// Status back so callers do not have to branch twice.
    application::Status report(application::Status status);

    // The database is declared first so that it outlives the repositories
    // borrowing its connection.
    std::unique_ptr<infrastructure::sqlite::Database> m_database;
    std::unique_ptr<infrastructure::sqlite::SqliteProfileRepository> m_profiles;
    std::unique_ptr<infrastructure::sqlite::SqliteAssetRepository> m_assets;
    std::unique_ptr<infrastructure::sqlite::SqliteLiabilityRepository> m_liabilities;
    std::unique_ptr<infrastructure::sqlite::SqliteDebtBalanceRepository> m_debtBalances;
    std::unique_ptr<infrastructure::sqlite::SqliteValuationRepository> m_valuations;
    std::unique_ptr<infrastructure::sqlite::SqliteMonthFlowRepository> m_flows;
    QString m_lastError;

    static AppController* s_instance;
};

} // namespace hinkwise::presentation
