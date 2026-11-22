// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_controller.h"

#include "application/use_cases.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"

#include <QDate>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QString>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

template <typename T> std::vector<T> rowsOrEmpty(const application::Result<std::vector<T>>& result)
{
    return result.ok() ? result.value() : std::vector<T>{};
}

std::string toStd(const QString& text)
{
    return text.toStdString();
}

} // namespace

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

domain::YearMonth AppController::currentMonth()
{
    const QDate today = QDate::currentDate();
    return domain::YearMonth{today.year(), static_cast<unsigned>(today.month())};
}

application::DashboardSnapshot AppController::dashboardSnapshot() const
{
    if (!m_profiles)
        return application::DashboardSnapshot{};

    return application::buildDashboard(*m_profiles, *m_assets, *m_liabilities, *m_debtBalances,
                                       *m_valuations, *m_flows, currentMonth());
}

std::optional<domain::Profile> AppController::profile() const
{
    if (!m_profiles)
        return std::nullopt;
    const application::Result<std::optional<domain::Profile>> loaded = m_profiles->load();
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::vector<domain::Asset> AppController::assets() const
{
    if (!m_assets)
        return {};
    return rowsOrEmpty(m_assets->all());
}

std::vector<domain::Liability> AppController::liabilities() const
{
    if (!m_liabilities)
        return {};
    return rowsOrEmpty(m_liabilities->all());
}

std::optional<domain::Money> AppController::latestAssetBalance(std::string_view assetId) const
{
    if (!m_valuations)
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded = m_valuations->latest(assetId);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::optional<domain::Money> AppController::assetBalanceAt(std::string_view assetId,
                                                           domain::YearMonth month) const
{
    if (!m_valuations)
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded =
        m_valuations->balanceAt(assetId, month);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::optional<domain::Money> AppController::latestDebtBalance(std::string_view liabilityId) const
{
    if (!m_debtBalances)
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded =
        m_debtBalances->latest(liabilityId);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

application::Status AppController::report(application::Status status)
{
    if (status.ok())
        emit dataChanged();
    return status;
}

application::Status AppController::createHousehold(domain::Currency currency, domain::Phase phase)
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));

    // The wizard does not ask for a name: the household is the only one this
    // application holds, and settings can rename it once that page exists.
    const application::Result<domain::Profile> created =
        application::createProfile(toStd(tr("Household")), currency, phase);
    if (!created.ok())
        return application::Status::err(created.error());

    return report(m_profiles->save(created.value()));
}

application::Status AppController::addAsset(const domain::Asset& asset)
{
    if (!m_assets)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::addAsset(*m_assets, asset));
}

application::Status AppController::removeAsset(std::string_view assetId)
{
    if (!m_assets)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::removeAsset(*m_assets, assetId));
}

application::Status AppController::addLiability(const domain::Liability& liability)
{
    if (!m_liabilities)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::addLiability(*m_liabilities, liability));
}

application::Status AppController::removeLiability(std::string_view liabilityId)
{
    if (!m_liabilities)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::removeLiability(*m_liabilities, liabilityId));
}

application::Status AppController::recordMonth(domain::YearMonth month,
                                               const domain::MonthFlow& flow)
{
    if (!m_flows)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::recordMonth(*m_flows, month, flow));
}

application::Status AppController::recordValuation(std::string_view assetId,
                                                   domain::YearMonth month, domain::Money balance)
{
    if (!m_valuations)
        return application::Status::err(toStd(tr("The database is not open.")));
    return report(application::recordValuation(*m_valuations, assetId, month, balance));
}

application::Status AppController::recordDebtBalance(std::string_view liabilityId,
                                                     domain::YearMonth month, domain::Money balance)
{
    // The application layer has a use case for a valuation but not yet for a
    // debt balance, so the two checks recordValuation() makes are made here
    // rather than skipped. Move them into the use case when it arrives.
    if (!m_debtBalances)
        return application::Status::err(toStd(tr("The database is not open.")));
    if (liabilityId.empty())
        return application::Status::err(toStd(tr("A debt balance needs a debt.")));
    if (balance.isNegative())
        return application::Status::err(toStd(tr("A debt balance cannot be negative.")));
    return report(m_debtBalances->upsert(liabilityId, month, balance));
}

} // namespace hinkwise::presentation
