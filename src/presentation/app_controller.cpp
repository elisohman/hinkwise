// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_controller.h"

#include "application/use_cases.h"
#include "domain/year_month.h"
#include "infrastructure/json/backup.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"

#include <QDate>
#include <QDateTime>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QString>
#include <algorithm>
#include <limits>
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

/// One amount at one month, moved from the application layer's port type to the
/// backup format's own. The two carry the same fields because both sit on the
/// storage edge; the conversion is here so neither layer has to know the other.
infrastructure::json::MoneyPoint toBackupPoint(const application::MoneyPoint& point)
{
    return infrastructure::json::MoneyPoint{point.month, point.amount};
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
    m_plannedExpenses =
        std::make_unique<infrastructure::sqlite::SqlitePlannedExpenseRepository>(connection);
    m_phaseHistory =
        std::make_unique<infrastructure::sqlite::SqlitePhaseHistoryRepository>(connection);

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

application::Status AppController::saveProfile(const domain::Profile& profile)
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));
    if (profile.displayName.empty())
        return application::Status::err(toStd(tr("A household needs a name.")));
    return report(m_profiles->save(profile));
}

application::Status AppController::changePhase(domain::Phase next)
{
    if (!m_profiles || !m_phaseHistory)
        return application::Status::err(toStd(tr("The database is not open.")));

    std::optional<domain::Profile> current = profile();
    if (!current.has_value())
        return application::Status::err(toStd(tr("There is no household to change phase for.")));

    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return report(
        application::changePhase(*m_profiles, *m_phaseHistory, *current, next, toStd(stamp)));
}

std::vector<application::PhaseChange> AppController::phaseHistory() const
{
    if (!m_phaseHistory)
        return {};
    return rowsOrEmpty(m_phaseHistory->all());
}

QString AppController::databasePath() const
{
    if (m_database)
        return m_database->path();
    return infrastructure::sqlite::Database::defaultPath();
}

application::Result<int> AppController::exportBackup(const QString& path) const
{
    using Outcome = application::Result<int>;

    if (!m_database || !m_profiles || !m_assets || !m_liabilities || !m_plannedExpenses ||
        !m_valuations || !m_debtBalances || !m_flows || !m_phaseHistory)
        return Outcome::err(toStd(tr("The database is not open.")));
    if (path.isEmpty())
        return Outcome::err(toStd(tr("A backup needs a file to be written to.")));

    const application::Result<std::optional<domain::Profile>> loaded = m_profiles->load();
    if (!loaded.ok())
        return Outcome::err(loaded.error());
    if (!loaded.value().has_value())
        return Outcome::err(toStd(tr("There is no household to back up yet.")));

    infrastructure::json::BackupData data;
    data.profile = *loaded.value();

    const application::Result<std::vector<domain::Asset>> assets = m_assets->all();
    if (!assets.ok())
        return Outcome::err(assets.error());
    data.assets = assets.value();

    const application::Result<std::vector<domain::Liability>> liabilities = m_liabilities->all();
    if (!liabilities.ok())
        return Outcome::err(liabilities.error());
    data.liabilities = liabilities.value();

    const application::Result<std::vector<domain::PlannedExpense>> expenses =
        m_plannedExpenses->all();
    if (!expenses.ok())
        return Outcome::err(expenses.error());
    data.plannedExpenses = expenses.value();

    int records = static_cast<int>(data.assets.size() + data.liabilities.size() +
                                   data.plannedExpenses.size());

    data.assetValuations.reserve(data.assets.size());
    for (const domain::Asset& asset : data.assets) {
        const application::Result<std::vector<application::MoneyPoint>> points =
            m_valuations->history(asset.id);
        if (!points.ok())
            return Outcome::err(points.error());

        infrastructure::json::AssetValuations entry;
        entry.assetId = asset.id;
        entry.points.reserve(points.value().size());
        for (const application::MoneyPoint& point : points.value())
            entry.points.push_back(toBackupPoint(point));
        records += static_cast<int>(entry.points.size());
        data.assetValuations.push_back(std::move(entry));
    }

    data.liabilityBalances.reserve(data.liabilities.size());
    for (const domain::Liability& liability : data.liabilities) {
        const application::Result<std::vector<application::MoneyPoint>> points =
            m_debtBalances->history(liability.id);
        if (!points.ok())
            return Outcome::err(points.error());

        infrastructure::json::LiabilityBalances entry;
        entry.liabilityId = liability.id;
        entry.points.reserve(points.value().size());
        for (const application::MoneyPoint& point : points.value())
            entry.points.push_back(toBackupPoint(point));
        records += static_cast<int>(entry.points.size());
        data.liabilityBalances.push_back(std::move(entry));
    }

    const application::Result<std::vector<application::MonthFlowRecord>> flows =
        m_flows->recent(std::numeric_limits<int>::max());
    if (!flows.ok())
        return Outcome::err(flows.error());
    data.monthFlows = flows.value();
    // recent() hands them back newest first because that is what a chart wants;
    // a file is read oldest first, where the first month is where the story
    // begins.
    std::sort(data.monthFlows.begin(), data.monthFlows.end(),
              [](const application::MonthFlowRecord& left,
                 const application::MonthFlowRecord& right) { return left.month < right.month; });
    records += static_cast<int>(data.monthFlows.size());

    const application::Result<std::vector<application::PhaseChange>> history =
        m_phaseHistory->all();
    if (!history.ok())
        return Outcome::err(history.error());
    data.phaseHistory = history.value();
    records += static_cast<int>(data.phaseHistory.size());

    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly))
        return Outcome::err(toStd(tr("Cannot open %1: %2").arg(path, file.errorString())));

    const QByteArray bytes = QByteArray::fromStdString(infrastructure::json::toJson(data));
    if (file.write(bytes) != bytes.size())
        return Outcome::err(toStd(tr("Cannot write %1: %2").arg(path, file.errorString())));
    if (!file.commit())
        return Outcome::err(
            toStd(tr("Cannot finish writing %1: %2").arg(path, file.errorString())));

    return Outcome::ok(records);
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

std::vector<application::MonthFlowRecord> AppController::recentFlows(int count) const
{
    if (!m_flows)
        return {};
    return rowsOrEmpty(m_flows->recent(count));
}

std::optional<domain::MonthFlow> AppController::monthFlow(domain::YearMonth month) const
{
    if (!m_flows)
        return std::nullopt;
    const application::Result<std::optional<domain::MonthFlow>> loaded = m_flows->at(month);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::vector<domain::PlannedExpense> AppController::plannedExpenses() const
{
    if (!m_plannedExpenses)
        return {};
    return rowsOrEmpty(m_plannedExpenses->all());
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

application::Status AppController::addPlannedExpense(const domain::PlannedExpense& expense)
{
    // The application layer has no use case for a planned expense yet, so the
    // checks one would make are made here, the same way recordDebtBalance()
    // carries its own. Move them into the use case when it arrives.
    if (!m_plannedExpenses)
        return application::Status::err(toStd(tr("The database is not open.")));
    if (expense.id.empty() || expense.name.empty())
        return application::Status::err(toStd(tr("A planned purchase needs a name.")));
    if (!expense.amount.isPositive())
        return application::Status::err(toStd(tr("A planned purchase needs an amount.")));

    return report(m_plannedExpenses->upsert(expense));
}

application::Status AppController::removePlannedExpense(std::string_view expenseId)
{
    if (!m_plannedExpenses)
        return application::Status::err(toStd(tr("The database is not open.")));
    if (expenseId.empty())
        return application::Status::err(toStd(tr("A planned purchase id is needed to remove it.")));
    return report(m_plannedExpenses->remove(expenseId));
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
