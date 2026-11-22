// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_controller.h"

#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"

#include <QDate>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QString>
#include <memory>
#include <string>
#include <utility>

namespace hinkwise::presentation {

AppController* AppController::s_instance = nullptr;

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // The executable owns this object; the QML engine only borrows it.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    s_instance = this;
}

AppController::~AppController()
{
    if (s_instance == this)
        s_instance = nullptr;
}

AppController* AppController::create(QQmlEngine* engine, QJSEngine*)
{
    // main() builds the controller before the engine exists, so this only has
    // to return that one. A QML-only entry point (a test, a preview) still gets
    // a controller rather than a null singleton.
    if (s_instance == nullptr)
        new AppController{engine};
    return s_instance;
}

AppController* AppController::instance()
{
    return s_instance;
}

bool AppController::initialise()
{
    if (m_database)
        return true;

    application::Result<infrastructure::sqlite::Database> opened =
        infrastructure::sqlite::Database::open(infrastructure::sqlite::Database::defaultPath());
    if (!opened.ok()) {
        m_lastError = QString::fromStdString(opened.error());
        return false;
    }
    m_database = std::make_unique<infrastructure::sqlite::Database>(std::move(opened.value()));

    const application::Status migrated = m_database->migrate();
    if (!migrated.ok()) {
        m_lastError = QString::fromStdString(migrated.error());
        m_database.reset();
        return false;
    }

    const QSqlDatabase connection = m_database->connection();
    m_profiles = std::make_unique<infrastructure::sqlite::SqliteProfileRepository>(connection);
    m_assets = std::make_unique<infrastructure::sqlite::SqliteAssetRepository>(connection);
    m_liabilities = std::make_unique<infrastructure::sqlite::SqliteLiabilityRepository>(connection);
    m_debtBalances =
        std::make_unique<infrastructure::sqlite::SqliteDebtBalanceRepository>(connection);
    m_valuations = std::make_unique<infrastructure::sqlite::SqliteValuationRepository>(connection);
    m_flows = std::make_unique<infrastructure::sqlite::SqliteMonthFlowRepository>(connection);

    m_lastError.clear();
    return true;
}

QString AppController::lastError() const
{
    return m_lastError;
}

application::DashboardSnapshot AppController::dashboardSnapshot() const
{
    if (!m_profiles)
        return application::DashboardSnapshot{};

    const QDate today = QDate::currentDate();
    const domain::YearMonth month{today.year(), static_cast<unsigned>(today.month())};
    return application::buildDashboard(*m_profiles, *m_assets, *m_liabilities, *m_debtBalances,
                                       *m_valuations, *m_flows, month);
}

} // namespace hinkwise::presentation
