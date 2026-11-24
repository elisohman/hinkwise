// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_controller.h"

#include "application/examples.h"
#include "application/use_cases.h"
#include "domain/year_month.h"
#include "infrastructure/json/backup.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"
#include "infrastructure/sqlite/sqlite_transaction.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QString>
#include <QUuid>
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

/// The form of `path` two paths can be compared in: the canonical one where the
/// file exists, the absolute one where it does not yet. A backup destination is
/// often a name that has never been written, while the database is always there.
QString comparablePath(const QString& path)
{
    const QFileInfo info{path};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

/// True when `destination` is the live database file or one of SQLite's own
/// sidecars. Writing a backup there replaces the household's data with JSON:
/// the database no longer opens, and nothing in the backup can be imported back
/// yet, so the loss is total.
bool namesTheDatabase(const QString& destination, const QString& databasePath)
{
    static constexpr const char* kSidecarSuffixes[] = {"", "-wal", "-shm", "-journal"};

    const QString target = comparablePath(destination);
    for (const char* suffix : kSidecarSuffixes) {
        const QString candidate = databasePath + QString::fromLatin1(suffix);
        if (target == comparablePath(candidate))
            return true;
    }
    return false;
}

/// The id a copied holding gets. Asset, liability and expense ids are unique
/// across the whole file, so a duplicate has to mint its own rather than carry
/// the source's ids into a second profile.
std::string copiedId(const char* prefix)
{
    return std::string{prefix} + "-" +
           QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

/// "Household", or the first free "Household 2", "Household 3": two empty
/// profiles are told apart in the list before either has been through
/// onboarding, and onboarding keeps the name it finds.
std::string availableHouseholdName(const std::vector<application::ProfileSummary>& existing)
{
    const auto taken = [&existing](const std::string& name) {
        return std::any_of(existing.cbegin(), existing.cend(),
                           [&name](const application::ProfileSummary& summary) {
                               return summary.displayName == name;
                           });
    };

    if (!taken("Household"))
        return "Household";
    for (int suffix = 2;; ++suffix) {
        std::string candidate = "Household " + std::to_string(suffix);
        if (!taken(candidate))
            return candidate;
    }
}

} // namespace

/// The transaction beginTransaction() hands out.
///
/// It is the SQLite transaction plus the one fact the controller needs: when it
/// ended. While one is open dataChanged() is held back, so no screen reloads
/// and reads rows that a rollback may throw away; a commit announces the whole
/// transaction in one signal, and a rollback announces nothing because nothing
/// outside the transaction changed.
///
/// It refers to its controller, which owns the database it runs on, so it must
/// not outlive the controller: the holder keeps it in the scope of the
/// operation that began it.
class AppController::OpenTransaction final : public application::Transaction {
public:
    OpenTransaction(std::unique_ptr<application::Transaction> inner, AppController& owner)
        : m_inner(std::move(inner))
        , m_owner(owner)
    {
    }

    ~OpenTransaction() override
    {
        if (m_committed)
            return;

        // Roll back before the controller is told, so that whatever a slot does
        // on the way out already reads what the database strips back to.
        m_inner.reset();
        m_owner.endTransaction(false);
    }

    application::Status commit() override
    {
        m_committed = true;
        const application::Status committed = m_inner->commit();
        m_owner.endTransaction(committed.ok());
        return committed;
    }

private:
    std::unique_ptr<application::Transaction> m_inner;
    AppController& m_owner;
    bool m_committed = false;
};

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
    m_transactions = std::make_unique<infrastructure::sqlite::SqliteTransactionFactory>(connection);

    // The meta row is what remembers which household was being read last. A
    // file written before profiles existed has none, so the only profile there
    // is takes over; a file with no profile at all leaves every scoped
    // repository unset until the wizard creates one.
    const application::Status activated = activateStoredProfile();
    if (!activated.ok()) {
        m_lastError = QString::fromStdString(activated.error());
        m_transactions.reset();
        m_phaseHistory.reset();
        m_plannedExpenses.reset();
        m_flows.reset();
        m_valuations.reset();
        m_debtBalances.reset();
        m_liabilities.reset();
        m_assets.reset();
        m_profiles.reset();
        m_database.reset();
        return false;
    }

    m_lastError.clear();
    return true;
}

void AppController::pointScopedRepositoriesAt(std::int64_t profileId) const
{
    if (m_assets)
        m_assets->setActiveProfile(profileId);
    if (m_liabilities)
        m_liabilities->setActiveProfile(profileId);
    if (m_plannedExpenses)
        m_plannedExpenses->setActiveProfile(profileId);
    if (m_flows)
        m_flows->setActiveProfile(profileId);
    if (m_phaseHistory)
        m_phaseHistory->setActiveProfile(profileId);
    if (m_valuations)
        m_valuations->setActiveProfile(profileId);
    if (m_debtBalances)
        m_debtBalances->setActiveProfile(profileId);
}

application::Status AppController::activateStoredProfile() const
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));

    const application::Result<std::optional<domain::Profile>> active = m_profiles->load();
    if (!active.ok())
        return application::Status::err(active.error());

    if (active.value()) {
        m_activeProfileId = active.value()->id;
        pointScopedRepositoriesAt(m_activeProfileId);
        return application::Status::success();
    }

    // No pointer stored yet. A database that already holds a profile - written
    // by the release before profiles, or one whose meta was never filled in -
    // settles on the lowest id rather than making the household look empty.
    const application::Result<std::vector<application::ProfileSummary>> listed = m_profiles->list();
    if (!listed.ok())
        return application::Status::err(listed.error());
    if (listed.value().empty()) {
        m_activeProfileId = 0;
        pointScopedRepositoriesAt(0);
        return application::Status::success();
    }

    const application::ProfileSummary& first = listed.value().front();
    const application::Status activated = m_profiles->setActive(first.id);
    if (!activated.ok())
        return activated;

    m_activeProfileId = first.id;
    pointScopedRepositoriesAt(m_activeProfileId);
    return application::Status::success();
}

application::Status AppController::syncActiveProfile() const
{
    return activateStoredProfile();
}

qint64 AppController::activeProfileId() const
{
    // Reading it through meta rather than the cached value is what makes two
    // connections to one file agree.
    if (m_profiles && !syncActiveProfile().ok())
        return 0;
    return m_activeProfileId;
}

application::Status AppController::setActiveProfile(std::int64_t id)
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));

    // A switch in the middle of a transaction would move the repositories to a
    // household while the transaction still holds writes for the old one, and
    // a rollback would leave the two disagreeing.
    if (m_openTransactions > 0)
        return application::Status::err(
            toStd(tr("A household cannot be switched while a transaction is open.")));

    const application::Status activated = m_profiles->setActive(id);
    if (!activated.ok())
        return activated;

    m_activeProfileId = id;
    pointScopedRepositoriesAt(id);

    // Every screen reloads from the controller, so the switch reaches all of
    // them through the same signal an ordinary write uses.
    emit dataChanged();
    return application::Status::success();
}

std::vector<application::ProfileSummary> AppController::profiles() const
{
    if (!m_profiles)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_profiles->list());
}

application::Result<domain::Profile> AppController::createEmptyProfile()
{
    using Outcome = application::Result<domain::Profile>;

    if (!m_profiles)
        return Outcome::err(toStd(tr("The database is not open.")));

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return Outcome::err(synced.error());

    const application::Result<std::vector<application::ProfileSummary>> listed = m_profiles->list();
    if (!listed.ok())
        return Outcome::err(listed.error());

    // Empty means empty: the profile exists so the wizard has something to fill
    // in, and every setting the wizard asks for is answered there. Its default
    // currency and phase are therefore placeholders, not the household's choice.
    const application::Result<domain::Profile> draft =
        application::createProfile(availableHouseholdName(listed.value()), domain::Currency::Sek,
                                   domain::Phase::BuildingWealth);
    if (!draft.ok())
        return Outcome::err(draft.error());

    application::Result<std::unique_ptr<application::Transaction>> begun = beginTransaction();
    if (!begun.ok())
        return Outcome::err(begun.error());
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    const application::Result<domain::Profile> created = m_profiles->create(draft.value());
    if (!created.ok()) {
        transaction.reset();
        return Outcome::err(created.error());
    }

    // The new row is the stored active profile now, so the scoped repositories
    // follow it before anything reads from them or a screen reloads.
    const application::Status activated = activateStoredProfile();
    if (!activated.ok()) {
        transaction.reset();
        return Outcome::err(activated.error());
    }

    // The writes above went through the repositories directly, so the open
    // transaction has not been told there is anything to announce. Marking it
    // changed makes the commit announce the whole profile once.
    report(application::Status::success());

    const application::Status committed = transaction->commit();
    if (!committed.ok())
        return Outcome::err(committed.error());
    return Outcome::ok(created.value());
}

application::Result<domain::Profile> AppController::duplicateProfile(std::int64_t sourceId,
                                                                     const std::string& name)
{
    using Outcome = application::Result<domain::Profile>;

    if (!m_database || !m_profiles || !m_assets || !m_liabilities || !m_plannedExpenses ||
        !m_valuations || !m_debtBalances || !m_flows || !m_phaseHistory)
        return Outcome::err(toStd(tr("The database is not open.")));
    if (name.empty())
        return Outcome::err(toStd(tr("A duplicated profile needs a name.")));

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return Outcome::err(synced.error());

    const application::Result<std::optional<domain::Profile>> source =
        m_profiles->loadById(sourceId);
    if (!source.ok())
        return Outcome::err(source.error());
    if (!source.value().has_value())
        return Outcome::err(toStd(tr("That profile does not exist.")));

    // The copy is the household's own from the moment it exists. Duplicating an
    // example is how the household takes the story over, so the copy carries no
    // example flag; and because nothing below points back at the source, the two
    // are independent records - a change in one is not a change in the other.
    domain::Profile copy = *source.value();
    copy.id = 0;
    copy.displayName = name;
    copy.isExample = false;

    application::Result<std::unique_ptr<application::Transaction>> begun = beginTransaction();
    if (!begun.ok())
        return Outcome::err(begun.error());
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    const application::Result<domain::Profile> created = m_profiles->create(copy);
    if (!created.ok()) {
        transaction.reset();
        return Outcome::err(created.error());
    }

    const application::Status activated = activateStoredProfile();
    if (!activated.ok()) {
        transaction.reset();
        return Outcome::err(activated.error());
    }

    // The source is read through its own repositories on the same connection,
    // while the controller's own repositories are pointed at the copy. Nothing
    // is moved: the source keeps every row it had.
    const QSqlDatabase connection = m_database->connection();
    infrastructure::sqlite::SqliteAssetRepository sourceAssets{connection};
    infrastructure::sqlite::SqliteLiabilityRepository sourceLiabilities{connection};
    infrastructure::sqlite::SqlitePlannedExpenseRepository sourceExpenses{connection};
    infrastructure::sqlite::SqliteValuationRepository sourceValuations{connection};
    infrastructure::sqlite::SqliteDebtBalanceRepository sourceDebtBalances{connection};
    infrastructure::sqlite::SqliteMonthFlowRepository sourceFlows{connection};
    infrastructure::sqlite::SqlitePhaseHistoryRepository sourceHistory{connection};

    sourceAssets.setActiveProfile(sourceId);
    sourceLiabilities.setActiveProfile(sourceId);
    sourceExpenses.setActiveProfile(sourceId);
    sourceValuations.setActiveProfile(sourceId);
    sourceDebtBalances.setActiveProfile(sourceId);
    sourceFlows.setActiveProfile(sourceId);
    sourceHistory.setActiveProfile(sourceId);

    const auto abandon = [&transaction](const std::string& message) {
        transaction.reset();
        return Outcome::err(message);
    };

    const application::Result<std::vector<domain::Asset>> assets = sourceAssets.all();
    if (!assets.ok())
        return abandon(assets.error());
    for (const domain::Asset& asset : assets.value()) {
        domain::Asset assetCopy = asset;
        assetCopy.id = copiedId("asset");
        const application::Status stored = m_assets->upsert(assetCopy);
        if (!stored.ok())
            return abandon(stored.error());

        const application::Result<std::vector<application::MoneyPoint>> history =
            sourceValuations.history(asset.id);
        if (!history.ok())
            return abandon(history.error());
        for (const application::MoneyPoint& point : history.value()) {
            const application::Status valued =
                m_valuations->upsert(assetCopy.id, point.month, point.amount);
            if (!valued.ok())
                return abandon(valued.error());
        }
    }

    const application::Result<std::vector<domain::Liability>> liabilities = sourceLiabilities.all();
    if (!liabilities.ok())
        return abandon(liabilities.error());
    for (const domain::Liability& liability : liabilities.value()) {
        domain::Liability liabilityCopy = liability;
        liabilityCopy.id = copiedId("liability");
        const application::Status stored = m_liabilities->upsert(liabilityCopy);
        if (!stored.ok())
            return abandon(stored.error());

        const application::Result<std::vector<application::MoneyPoint>> history =
            sourceDebtBalances.history(liability.id);
        if (!history.ok())
            return abandon(history.error());
        for (const application::MoneyPoint& point : history.value()) {
            const application::Status balanced =
                m_debtBalances->upsert(liabilityCopy.id, point.month, point.amount);
            if (!balanced.ok())
                return abandon(balanced.error());
        }
    }

    const application::Result<std::vector<domain::PlannedExpense>> expenses = sourceExpenses.all();
    if (!expenses.ok())
        return abandon(expenses.error());
    for (const domain::PlannedExpense& expense : expenses.value()) {
        domain::PlannedExpense expenseCopy = expense;
        expenseCopy.id = copiedId("expense");
        const application::Status stored = m_plannedExpenses->upsert(expenseCopy);
        if (!stored.ok())
            return abandon(stored.error());
    }

    const application::Result<std::vector<application::MonthFlowRecord>> flows =
        sourceFlows.recent(std::numeric_limits<int>::max());
    if (!flows.ok())
        return abandon(flows.error());
    for (const application::MonthFlowRecord& record : flows.value()) {
        const application::Status stored = m_flows->upsert(record.month, record.flow);
        if (!stored.ok())
            return abandon(stored.error());
    }

    const application::Result<std::vector<application::PhaseChange>> history = sourceHistory.all();
    if (!history.ok())
        return abandon(history.error());
    for (const application::PhaseChange& change : history.value()) {
        const application::Status stored = m_phaseHistory->append(change);
        if (!stored.ok())
            return abandon(stored.error());
    }

    // The writes above went through the repositories directly, so the open
    // transaction has not been told there is anything to announce. Marking it
    // changed makes the commit announce the whole profile once.
    report(application::Status::success());

    const application::Status committed = transaction->commit();
    if (!committed.ok())
        return Outcome::err(committed.error());
    return Outcome::ok(created.value());
}

application::Result<domain::Profile>
AppController::createExampleProfile(application::ExampleProfile kind)
{
    using Outcome = application::Result<domain::Profile>;

    if (!m_profiles || !m_assets || !m_liabilities || !m_flows || !m_valuations || !m_debtBalances)
        return Outcome::err(toStd(tr("The database is not open.")));

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return Outcome::err(synced.error());

    application::Result<std::unique_ptr<application::Transaction>> begun = beginTransaction();
    if (!begun.ok())
        return Outcome::err(begun.error());
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    const application::Result<domain::Profile> created =
        m_profiles->create(application::exampleProfile(kind));
    if (!created.ok()) {
        transaction.reset();
        return Outcome::err(created.error());
    }

    const application::Status activated = activateStoredProfile();
    if (!activated.ok()) {
        transaction.reset();
        return Outcome::err(activated.error());
    }

    const application::Status seeded =
        application::seedExample(kind, created.value().id, *m_assets, *m_liabilities, *m_flows,
                                 *m_valuations, *m_debtBalances, currentMonth());
    if (!seeded.ok()) {
        transaction.reset();
        return Outcome::err(seeded.error());
    }

    // The writes above went through the repositories directly, so the open
    // transaction has not been told there is anything to announce. Marking it
    // changed makes the commit announce the whole profile once.
    report(application::Status::success());

    const application::Status committed = transaction->commit();
    if (!committed.ok())
        return Outcome::err(committed.error());
    return Outcome::ok(created.value());
}

application::Status AppController::removeProfile(std::int64_t id)
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;

    const application::Result<std::vector<application::ProfileSummary>> listed = m_profiles->list();
    if (!listed.ok())
        return application::Status::err(listed.error());

    // The repository refuses this too, but its message names the operation. What
    // the household has to read is why one profile has to stay.
    if (listed.value().size() <= 1)
        return application::Status::err(
            toStd(tr("The last profile cannot be deleted: Hinkwise needs one household to show.")));

    const application::Status removed = m_profiles->remove(id);
    if (!removed.ok())
        return removed;

    // Removing the active profile moves the stored pointer to the household that
    // takes over, and the scoped repositories have to move with it before any
    // screen reloads and reads the wrong one.
    const application::Status activated = activateStoredProfile();
    if (!activated.ok())
        return activated;
    return report(application::Status::success());
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
    if (!syncActiveProfile().ok())
        return application::DashboardSnapshot{};

    const application::Result<application::DashboardSnapshot> built =
        application::buildDashboard(*m_profiles, *m_assets, *m_liabilities, *m_debtBalances,
                                    *m_valuations, *m_flows, currentMonth());
    // buildDashboard() only fails when it cannot read when the inputs were last
    // recorded. The screens are not built around that error yet, so it leaves
    // the empty household rather than a snapshot claiming to be fresh; the
    // empty state is a visible prompt to record something instead.
    return built.ok() ? built.value() : application::DashboardSnapshot{};
}

std::optional<domain::Profile> AppController::profile() const
{
    if (!m_profiles)
        return std::nullopt;
    if (!syncActiveProfile().ok())
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

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;

    const application::Status saved = m_profiles->save(profile);
    if (!saved.ok())
        return saved;

    // Saving the first profile also makes it active, so the scoped
    // repositories have to follow before any screen reloads and reads them.
    const application::Status activated = activateStoredProfile();
    if (!activated.ok())
        return activated;
    return report(application::Status::success());
}

application::Status AppController::changePhase(domain::Phase next)
{
    if (!m_profiles || !m_phaseHistory)
        return application::Status::err(toStd(tr("The database is not open.")));

    std::optional<domain::Profile> current = profile();
    if (!current.has_value())
        return application::Status::err(toStd(tr("There is no household to change phase for.")));

    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return report(application::changePhase(*m_profiles, *m_phaseHistory, *this, *current, next,
                                           toStd(stamp)));
}

std::vector<application::PhaseChange> AppController::phaseHistory() const
{
    if (!m_phaseHistory)
        return {};
    if (!syncActiveProfile().ok())
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

    const QString databasePath = m_database->path();
    if (namesTheDatabase(path, databasePath))
        return Outcome::err(
            toStd(tr("That is the database file (%1). Choose another name: a backup cannot "
                     "replace the household's data.")
                      .arg(databasePath)));

    const application::Result<std::vector<application::ProfileSummary>> listed = m_profiles->list();
    if (!listed.ok())
        return Outcome::err(listed.error());
    if (listed.value().empty())
        return Outcome::err(toStd(tr("There is no household to back up yet.")));

    // A backup is of the whole database, not of whichever household happens to
    // be active. The repositories are pointed at each profile in turn on the
    // controller's own connection, without changing meta: the stored pointer is
    // left exactly where the user had it.
    const QSqlDatabase connection = m_database->connection();
    infrastructure::sqlite::SqliteAssetRepository assets{connection};
    infrastructure::sqlite::SqliteLiabilityRepository liabilities{connection};
    infrastructure::sqlite::SqlitePlannedExpenseRepository expenses{connection};
    infrastructure::sqlite::SqliteValuationRepository valuations{connection};
    infrastructure::sqlite::SqliteDebtBalanceRepository debtBalances{connection};
    infrastructure::sqlite::SqliteMonthFlowRepository flows{connection};
    infrastructure::sqlite::SqlitePhaseHistoryRepository phaseHistory{connection};

    infrastructure::json::Backup backup;
    int records = 0;

    for (const application::ProfileSummary& summary : listed.value()) {
        const application::Result<std::optional<domain::Profile>> loaded =
            m_profiles->loadById(summary.id);
        if (!loaded.ok())
            return Outcome::err(loaded.error());
        if (!loaded.value().has_value())
            return Outcome::err("backup: profile " + std::to_string(summary.id) +
                                " does not exist");

        infrastructure::json::BackupData data;
        data.profile = *loaded.value();

        assets.setActiveProfile(summary.id);
        liabilities.setActiveProfile(summary.id);
        expenses.setActiveProfile(summary.id);
        valuations.setActiveProfile(summary.id);
        debtBalances.setActiveProfile(summary.id);
        flows.setActiveProfile(summary.id);
        phaseHistory.setActiveProfile(summary.id);

        const application::Result<std::vector<domain::Asset>> profileAssets = assets.all();
        if (!profileAssets.ok())
            return Outcome::err(profileAssets.error());
        data.assets = profileAssets.value();

        const application::Result<std::vector<domain::Liability>> profileLiabilities =
            liabilities.all();
        if (!profileLiabilities.ok())
            return Outcome::err(profileLiabilities.error());
        data.liabilities = profileLiabilities.value();

        const application::Result<std::vector<domain::PlannedExpense>> profileExpenses =
            expenses.all();
        if (!profileExpenses.ok())
            return Outcome::err(profileExpenses.error());
        data.plannedExpenses = profileExpenses.value();

        records += static_cast<int>(data.assets.size() + data.liabilities.size() +
                                    data.plannedExpenses.size());

        data.assetValuations.reserve(data.assets.size());
        for (const domain::Asset& asset : data.assets) {
            const application::Result<std::vector<application::MoneyPoint>> points =
                valuations.history(asset.id);
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
                debtBalances.history(liability.id);
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

        const application::Result<std::vector<application::MonthFlowRecord>> profileFlows =
            flows.recent(std::numeric_limits<int>::max());
        if (!profileFlows.ok())
            return Outcome::err(profileFlows.error());
        data.monthFlows = profileFlows.value();
        // recent() hands them back newest first because that is what a chart
        // wants; a file is read oldest first, where the first month is where the
        // story begins.
        std::sort(
            data.monthFlows.begin(), data.monthFlows.end(),
            [](const application::MonthFlowRecord& left,
               const application::MonthFlowRecord& right) { return left.month < right.month; });
        records += static_cast<int>(data.monthFlows.size());

        const application::Result<std::vector<application::PhaseChange>> profileHistory =
            phaseHistory.all();
        if (!profileHistory.ok())
            return Outcome::err(profileHistory.error());
        data.phaseHistory = profileHistory.value();
        records += static_cast<int>(data.phaseHistory.size());

        backup.profiles.push_back(std::move(data));
    }

    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly))
        return Outcome::err(toStd(tr("Cannot open %1: %2").arg(path, file.errorString())));

    const QByteArray bytes = QByteArray::fromStdString(infrastructure::json::toJson(backup));
    if (file.write(bytes) != bytes.size())
        return Outcome::err(toStd(tr("Cannot write %1: %2").arg(path, file.errorString())));
    if (!file.commit())
        return Outcome::err(
            toStd(tr("Cannot finish writing %1: %2").arg(path, file.errorString())));

    // A backup holds the whole balance sheet, so it is readable by its owner
    // alone. Refusing to leave a world-readable copy is part of having written
    // one at all.
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return Outcome::err(toStd(tr("Cannot set owner-only permissions on %1.").arg(path)));

    return Outcome::ok(records);
}

std::vector<domain::Asset> AppController::assets() const
{
    if (!m_assets)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_assets->all());
}

std::vector<domain::Liability> AppController::liabilities() const
{
    if (!m_liabilities)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_liabilities->all());
}

std::optional<domain::Money> AppController::latestAssetBalance(std::string_view assetId) const
{
    if (!m_valuations)
        return std::nullopt;
    if (!syncActiveProfile().ok())
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded = m_valuations->latest(assetId);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::optional<domain::Money> AppController::assetBalanceAt(std::string_view assetId,
                                                           domain::YearMonth month) const
{
    if (!m_valuations)
        return std::nullopt;
    if (!syncActiveProfile().ok())
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded =
        m_valuations->balanceAt(assetId, month);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::vector<application::MoneyPoint> AppController::assetHistory(std::string_view assetId) const
{
    if (!m_valuations)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_valuations->history(assetId));
}

std::vector<application::MoneyPoint> AppController::debtHistory(std::string_view liabilityId) const
{
    if (!m_debtBalances)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_debtBalances->history(liabilityId));
}

std::optional<domain::Money> AppController::latestDebtBalance(std::string_view liabilityId) const
{
    if (!m_debtBalances)
        return std::nullopt;
    if (!syncActiveProfile().ok())
        return std::nullopt;
    const application::Result<std::optional<domain::Money>> loaded =
        m_debtBalances->latest(liabilityId);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::vector<application::MonthFlowRecord> AppController::recentFlows(int count) const
{
    if (!m_flows)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_flows->recent(count));
}

std::optional<domain::MonthFlow> AppController::monthFlow(domain::YearMonth month) const
{
    if (!m_flows)
        return std::nullopt;
    if (!syncActiveProfile().ok())
        return std::nullopt;
    const application::Result<std::optional<domain::MonthFlow>> loaded = m_flows->at(month);
    return loaded.ok() ? loaded.value() : std::nullopt;
}

std::vector<domain::PlannedExpense> AppController::plannedExpenses() const
{
    if (!m_plannedExpenses)
        return {};
    if (!syncActiveProfile().ok())
        return {};
    return rowsOrEmpty(m_plannedExpenses->all());
}

application::Result<std::unique_ptr<application::Transaction>> AppController::beginTransaction()
{
    using Outcome = application::Result<std::unique_ptr<application::Transaction>>;

    if (!m_transactions)
        return Outcome::err(toStd(tr("The database is not open.")));

    application::Result<std::unique_ptr<application::Transaction>> begun = m_transactions->begin();
    if (!begun.ok())
        return Outcome::err(begun.error());

    ++m_openTransactions;
    m_changedInTransaction = false;
    return Outcome::ok(std::make_unique<OpenTransaction>(std::move(begun.value()), *this));
}

void AppController::endTransaction(bool committed)
{
    if (m_openTransactions > 0)
        --m_openTransactions;
    if (m_openTransactions > 0)
        return;

    // A rollback, or a commit that failed and rolled itself back, restores the
    // stored pointer. The repository pointers are moved back with it; otherwise
    // a household created inside the transaction would stay the one the
    // repositories read after the database had already forgotten it.
    if (!committed && !syncActiveProfile().ok()) {
        m_activeProfileId = 0;
        pointScopedRepositoriesAt(0);
    }

    const bool announce = committed && m_changedInTransaction;
    m_changedInTransaction = false;
    if (announce)
        emit dataChanged();
}

application::Status AppController::report(application::Status status)
{
    if (!status.ok())
        return status;

    // Inside a transaction nothing is announced yet: a screen that reloaded now
    // would read rows the commit can still throw away. The commit announces
    // them once, for the whole transaction.
    if (m_openTransactions > 0) {
        m_changedInTransaction = true;
        return status;
    }

    emit dataChanged();
    return status;
}

application::Status AppController::createHousehold(domain::Currency currency, domain::Phase phase,
                                                   domain::TrackingMode trackingMode)
{
    if (!m_profiles)
        return application::Status::err(toStd(tr("The database is not open.")));

    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;

    // The wizard is two things: the first run, which names the household, and
    // the way a profile created from settings is filled in. In the second case
    // the profile already has the name settings gave it, and finishing
    // onboarding must not rename what the household just called itself.
    const std::optional<domain::Profile> current = profile();
    const std::string name = current.has_value() ? current->displayName : toStd(tr("Household"));
    const application::Result<domain::Profile> created =
        application::createProfile(name, currency, phase);
    if (!created.ok())
        return application::Status::err(created.error());

    domain::Profile profile = created.value();
    profile.trackingMode = trackingMode;

    const application::Status saved = m_profiles->save(profile);
    if (!saved.ok())
        return saved;

    // The first household a file holds becomes the active one, which is what
    // the screens that are about to reload will read.
    const application::Status activated = activateStoredProfile();
    if (!activated.ok())
        return activated;
    return report(application::Status::success());
}

application::Status AppController::addAsset(const domain::Asset& asset)
{
    if (!m_assets)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::addAsset(*m_assets, asset));
}

application::Status AppController::removeAsset(std::string_view assetId)
{
    if (!m_assets)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::removeAsset(*m_assets, assetId));
}

application::Status AppController::addLiability(const domain::Liability& liability)
{
    if (!m_liabilities)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::addLiability(*m_liabilities, liability));
}

application::Status AppController::updateLiability(const domain::Liability& liability)
{
    if (!m_liabilities)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::updateLiability(*m_liabilities, liability));
}

application::Status AppController::removeLiability(std::string_view liabilityId)
{
    if (!m_liabilities)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::removeLiability(*m_liabilities, liabilityId));
}

application::Status AppController::addPlannedExpense(const domain::PlannedExpense& expense)
{
    // The application layer has no use case for a planned expense yet, so the
    // checks one would make are made here, the same way recordDebtBalance()
    // carries its own. Move them into the use case when it arrives.
    if (!m_plannedExpenses)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
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
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    if (expenseId.empty())
        return application::Status::err(toStd(tr("A planned purchase id is needed to remove it.")));
    return report(m_plannedExpenses->remove(expenseId));
}

application::Status AppController::recordMonth(domain::YearMonth month,
                                               const domain::MonthFlow& flow)
{
    if (!m_flows)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::recordMonth(*m_flows, month, flow));
}

application::Status AppController::recordValuation(std::string_view assetId,
                                                   domain::YearMonth month, domain::Money balance)
{
    if (!m_valuations)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::recordValuation(*m_valuations, assetId, month, balance));
}

application::Status AppController::recordDebtBalance(std::string_view liabilityId,
                                                     domain::YearMonth month, domain::Money balance)
{
    if (!m_debtBalances)
        return application::Status::err(toStd(tr("The database is not open.")));
    const application::Status synced = syncActiveProfile();
    if (!synced.ok())
        return synced;
    return report(application::recordDebtBalance(*m_debtBalances, liabilityId, month, balance));
}

} // namespace hinkwise::presentation
