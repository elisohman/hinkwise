// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <memory>

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
/// It owns the SQLite database and one repository per port, and hands the
/// dashboard view model a snapshot. QML reaches it as the `AppController`
/// singleton; the executable creates it before the QML engine loads, so the
/// database is open by the time the first screen asks for numbers.
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

private:
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
