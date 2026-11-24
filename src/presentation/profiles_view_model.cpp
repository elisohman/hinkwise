// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profiles_view_model.h"

#include "app_controller.h"
#include "application/examples.h"
#include "application/ports.h"
#include "catalog.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/serialisation.h"

#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace hinkwise::presentation {

namespace {

/// The storage key a summary carries as text, read back as the mode the
/// catalogue names it by. A key no build knows is shown as the mode itself
/// rather than as an empty cell.
domain::TrackingMode trackingModeOf(const application::ProfileSummary& summary)
{
    const std::optional<domain::TrackingMode> mode =
        domain::trackingModeFromStorageKey(summary.trackingMode);
    return mode.value_or(domain::TrackingMode::Planning);
}

} // namespace

ProfilesViewModel::ProfilesViewModel(QObject* parent)
    : QObject(parent)
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &ProfilesViewModel::reload);
    }
    reload();
}

void ProfilesViewModel::reload()
{
    m_profiles.clear();

    // A refusal describes work that was unsaved when the switch was asked for.
    // Anything reloaded - a save, a discard, a write elsewhere - means the
    // household has dealt with something, so the message is not kept beyond it.
    m_switchError.clear();

    const AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_activeId = 0;
        m_activeName.clear();
        m_activeTrackingModeLabel.clear();
        m_activeIsExample = false;
        emit changed();
        return;
    }

    m_activeId = controller->activeProfileId();

    const std::vector<application::ProfileSummary> listed = controller->profiles();
    for (const application::ProfileSummary& summary : listed) {
        const bool active = summary.id == m_activeId;
        m_profiles.append(QVariantMap{
            {QStringLiteral("id"), static_cast<qlonglong>(summary.id)},
            {QStringLiteral("name"), QString::fromStdString(summary.displayName)},
            {QStringLiteral("trackingModeLabel"),
             catalog::trackingModeLabel(trackingModeOf(summary))},
            {QStringLiteral("phaseLabel"), QString::fromUtf8(domain::labelOf(summary.phase))},
            {QStringLiteral("isExample"), summary.isExample},
            {QStringLiteral("isActive"), active},
        });

        if (!active)
            continue;
        m_activeName = QString::fromStdString(summary.displayName);
        m_activeTrackingModeLabel = catalog::trackingModeLabel(trackingModeOf(summary));
        m_activeIsExample = summary.isExample;
    }

    emit changed();
}

QString ProfilesViewModel::blockingReason() const
{
    QStringList blockers;
    if (m_updateFlowOpen)
        blockers.append(tr("the update flow is open"));
    if (m_settingsDirty)
        blockers.append(tr("Settings has unsaved changes"));
    if (m_cashflowDirty)
        blockers.append(tr("Cashflow has unsaved changes"));

    if (blockers.isEmpty())
        return {};
    return tr("Nothing was switched: %1. Finish or discard that work first, then choose the "
              "profile again.")
        .arg(blockers.join(QStringLiteral(", ")));
}

QString ProfilesViewModel::switchTo(int profileId)
{
    m_switchError.clear();
    m_actionMessage.clear();

    if (profileId == m_activeId) {
        emit changed();
        return {};
    }

    // Work that is on screen but not saved belongs to the household that is
    // open now. A switch that took it away would lose it, so the switch waits
    // and says what has to be dealt with instead of discarding anything.
    const QString blocked = blockingReason();
    if (!blocked.isEmpty()) {
        m_switchError = blocked;
        emit changed();
        return m_switchError;
    }

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_switchError = tr("The database is not open.");
        emit changed();
        return m_switchError;
    }

    const application::Status switched = controller->setActiveProfile(profileId);
    if (!switched.ok()) {
        m_switchError = QString::fromStdString(switched.error());
        emit changed();
        return m_switchError;
    }

    // The controller announces the switch itself, so reload() has already run
    // and every screen shows the new household.
    emit changed();
    return {};
}

QString ProfilesViewModel::createNewProfile()
{
    m_actionError.clear();
    m_actionMessage.clear();

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_actionError = tr("The database is not open.");
        emit changed();
        return m_actionError;
    }

    const application::Result<domain::Profile> created = controller->createEmptyProfile();
    if (!created.ok()) {
        m_actionError = QString::fromStdString(created.error());
        emit changed();
        return m_actionError;
    }

    // The empty profile is active and every screen has already reloaded; the
    // shell now replaces the pages with the wizard, which fills it in.
    emit changed();
    emit onboardingRequested();
    return {};
}

QString ProfilesViewModel::beginDuplicate(int profileId)
{
    m_actionError.clear();
    m_actionMessage.clear();
    m_duplicateSourceId = 0;
    m_duplicateName.clear();

    const auto found =
        std::find_if(m_profiles.cbegin(), m_profiles.cend(), [profileId](const QVariant& row) {
            return row.toMap().value(QStringLiteral("id")).toLongLong() == profileId;
        });
    if (found == m_profiles.cend()) {
        m_actionError = tr("That profile does not exist.");
        emit changed();
        return m_actionError;
    }

    const QString sourceName = found->toMap().value(QStringLiteral("name")).toString();
    m_duplicateSourceId = profileId;
    // A name the household can keep or replace: the copy has to be told apart
    // from the profile it came from before it exists.
    m_duplicateName = tr("%1 copy").arg(sourceName);
    emit changed();
    return {};
}

QString ProfilesViewModel::confirmDuplicate()
{
    m_actionError.clear();
    m_actionMessage.clear();

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_actionError = tr("The database is not open.");
        emit changed();
        return m_actionError;
    }
    if (m_duplicateSourceId <= 0) {
        m_actionError = tr("Choose a profile to duplicate first.");
        emit changed();
        return m_actionError;
    }

    const application::Result<domain::Profile> copied =
        controller->duplicateProfile(m_duplicateSourceId, m_duplicateName.toStdString());
    if (!copied.ok()) {
        m_actionError = QString::fromStdString(copied.error());
        emit changed();
        return m_actionError;
    }

    m_duplicateSourceId = 0;
    m_duplicateName.clear();
    m_actionMessage = tr("The copy is its own household now: changes in one will not reach the "
                         "other.");
    emit changed();
    return {};
}

void ProfilesViewModel::cancelDuplicate()
{
    if (m_duplicateSourceId == 0)
        return;
    m_duplicateSourceId = 0;
    m_duplicateName.clear();
    emit changed();
}

QString ProfilesViewModel::createExample(int index)
{
    m_actionError.clear();
    m_actionMessage.clear();

    const int lastExample = static_cast<int>(application::ExampleProfile::Preserving);
    if (index < 0 || index > lastExample) {
        m_actionError = tr("That example does not exist.");
        emit changed();
        return m_actionError;
    }

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_actionError = tr("The database is not open.");
        emit changed();
        return m_actionError;
    }

    const auto kind = static_cast<application::ExampleProfile>(index);
    const application::Result<domain::Profile> created = controller->createExampleProfile(kind);
    if (!created.ok()) {
        m_actionError = QString::fromStdString(created.error());
        emit changed();
        return m_actionError;
    }

    m_actionMessage = tr("%1 is open now. Its figures are an example: delete it whenever you "
                         "like.")
                          .arg(QString::fromStdString(created.value().displayName));
    emit changed();
    return {};
}

QString ProfilesViewModel::deleteProfile(int profileId)
{
    m_actionError.clear();
    m_actionMessage.clear();

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_actionError = tr("The database is not open.");
        emit changed();
        return m_actionError;
    }

    const QString name = [this, profileId]() {
        const auto found =
            std::find_if(m_profiles.cbegin(), m_profiles.cend(), [profileId](const QVariant& row) {
                return row.toMap().value(QStringLiteral("id")).toLongLong() == profileId;
            });
        return found == m_profiles.cend() ? QString{}
                                          : found->toMap().value(QStringLiteral("name")).toString();
    }();

    const application::Status removed = controller->removeProfile(profileId);
    if (!removed.ok()) {
        m_actionError = QString::fromStdString(removed.error());
        emit changed();
        return m_actionError;
    }

    m_actionMessage =
        name.isEmpty() ? tr("The profile was deleted.") : tr("%1 was deleted.").arg(name);
    emit changed();
    return {};
}

QVariantList ProfilesViewModel::exampleChoices() const
{
    const auto choice = [](application::ExampleProfile kind, const QString& label,
                           const QString& description) {
        return QVariantMap{
            {QStringLiteral("index"), static_cast<int>(kind)},
            {QStringLiteral("label"), label},
            {QStringLiteral("description"), description},
        };
    };

    return {
        choice(application::ExampleProfile::BuildingWealth, tr("Example: building wealth"),
               tr("A household in its accumulating years: a buffer, index funds, a flat with a "
                  "mortgage, income pension, play money and twelve recorded months.")),
        choice(application::ExampleProfile::Preserving, tr("Example: preserving"),
               tr("The same household later: the buffer and preserve buckets carry the living "
                  "costs, the mortgage is gone, and the phase is Preserving.")),
    };
}

void ProfilesViewModel::setUpdateFlowOpen(bool open)
{
    if (open == m_updateFlowOpen)
        return;
    m_updateFlowOpen = open;
    emit blockersChanged();
}

void ProfilesViewModel::setCashflowDirty(bool dirty)
{
    if (dirty == m_cashflowDirty)
        return;
    m_cashflowDirty = dirty;
    emit blockersChanged();
}

void ProfilesViewModel::setSettingsDirty(bool dirty)
{
    if (dirty == m_settingsDirty)
        return;
    m_settingsDirty = dirty;
    emit blockersChanged();
}

void ProfilesViewModel::setDuplicateName(const QString& name)
{
    if (name == m_duplicateName)
        return;
    m_duplicateName = name;
    emit changed();
}

} // namespace hinkwise::presentation
