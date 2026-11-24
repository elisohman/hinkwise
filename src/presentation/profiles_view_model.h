// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <cstdint>

namespace hinkwise::presentation {

/// The households this database holds, as the settings list and the rail's
/// switcher read them.
///
/// One profile is one independent financial dataset: a name, how it keeps its
/// record, where it is on the journey, and whether its figures are an example.
/// This model is the only place that names a profile or decides which one is
/// active, so the list, the switcher and the shell's example badge cannot
/// disagree about which household is open.
///
/// Switching refuses while another screen holds unsaved work. The work is not
/// silently discarded: the model says which pages would lose something, and the
/// household finishes or discards it first.
class ProfilesViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// One entry per profile: id, name, trackingModeLabel, phaseLabel,
    /// isExample and isActive, ready for a list to draw.
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(qint64 activeId READ activeId NOTIFY changed)
    Q_PROPERTY(QString activeName READ activeName NOTIFY changed)
    Q_PROPERTY(QString activeTrackingModeLabel READ activeTrackingModeLabel NOTIFY changed)
    Q_PROPERTY(bool activeIsExample READ activeIsExample NOTIFY changed)

    /// The four example stories createExample() can write, in phase order. The
    /// labels are the names the profiles will carry, so the choice and the list
    /// row match.
    Q_PROPERTY(QVariantList exampleChoices READ exampleChoices CONSTANT)

    /// The screens that would lose unsaved work to a switch. QML binds each one
    /// to the page that owns it, because only the shell knows what is open.
    Q_PROPERTY(
        bool updateFlowOpen READ updateFlowOpen WRITE setUpdateFlowOpen NOTIFY blockersChanged)
    Q_PROPERTY(bool cashflowDirty READ cashflowDirty WRITE setCashflowDirty NOTIFY blockersChanged)
    Q_PROPERTY(bool settingsDirty READ settingsDirty WRITE setSettingsDirty NOTIFY blockersChanged)

    /// Why a switch did not happen, in words that name what to resolve. Empty
    /// after a switch that worked.
    Q_PROPERTY(QString switchError READ switchError NOTIFY changed)

    Q_PROPERTY(QString actionMessage READ actionMessage NOTIFY changed)
    Q_PROPERTY(QString actionError READ actionError NOTIFY changed)

    /// The name a duplicate is being given, asked for before it is written.
    Q_PROPERTY(QString duplicateName READ duplicateName WRITE setDuplicateName NOTIFY changed)

public:
    explicit ProfilesViewModel(QObject* parent = nullptr);

    /// Rebuilds the list from the controller. Called at construction and after
    /// every write, so a profile created on another screen appears here too.
    Q_INVOKABLE void reload();

    /// Switches to `profileId`, or refuses and sets switchError() when a screen
    /// holds unsaved work. Choosing the active profile again does nothing.
    /// Returns the same text as switchError().
    Q_INVOKABLE QString switchTo(int profileId);

    /// Creates an empty profile, makes it active and asks the shell to show
    /// onboarding for it. Returns the reason it could not.
    Q_INVOKABLE QString createNewProfile();

    /// Opens the name question for a copy of `profileId`, with a suggestion
    /// filled in. Nothing is written until confirmDuplicate().
    Q_INVOKABLE QString beginDuplicate(int profileId);

    /// Writes the copy under the name the household chose.
    Q_INVOKABLE QString confirmDuplicate();

    /// Closes the name question without writing anything.
    Q_INVOKABLE void cancelDuplicate();

    /// Writes one of the example stories and makes it active. `index` is a
    /// position in exampleChoices(), not a profile id.
    Q_INVOKABLE QString createExample(int index);

    /// Deletes the profile and everything recorded under it. Returns the
    /// reason it could not; the last profile cannot be deleted.
    Q_INVOKABLE QString deleteProfile(int profileId);

    QVariantList profiles() const { return m_profiles; }
    int count() const { return static_cast<int>(m_profiles.size()); }
    qint64 activeId() const { return m_activeId; }
    QString activeName() const { return m_activeName; }
    QString activeTrackingModeLabel() const { return m_activeTrackingModeLabel; }
    bool activeIsExample() const { return m_activeIsExample; }

    QVariantList exampleChoices() const;

    bool updateFlowOpen() const { return m_updateFlowOpen; }
    bool cashflowDirty() const { return m_cashflowDirty; }
    bool settingsDirty() const { return m_settingsDirty; }
    QString switchError() const { return m_switchError; }
    QString actionMessage() const { return m_actionMessage; }
    QString actionError() const { return m_actionError; }
    QString duplicateName() const { return m_duplicateName; }

    void setUpdateFlowOpen(bool open);
    void setCashflowDirty(bool dirty);
    void setSettingsDirty(bool dirty);
    void setDuplicateName(const QString& name);

signals:
    /// Anything the list, the switcher or the badge shows changed.
    void changed();

    /// One of the flags a switch has to respect changed. Separate from changed()
    /// because the list itself did not: only the refusal, if a switch is tried,
    /// would differ.
    void blockersChanged();

    /// A new empty profile was created and is active; the shell shows the
    /// onboarding wizard for it. Emitted after dataChanged(), so every screen
    /// has already reloaded when the wizard opens.
    void onboardingRequested();

private:
    /// The sentence that says which screens would lose work to a switch, or an
    /// empty string when nothing would.
    QString blockingReason() const;

    QVariantList m_profiles;
    qint64 m_activeId = 0;
    QString m_activeName;
    QString m_activeTrackingModeLabel;
    bool m_activeIsExample = false;

    bool m_updateFlowOpen = false;
    bool m_cashflowDirty = false;
    bool m_settingsDirty = false;

    QString m_switchError;
    QString m_actionMessage;
    QString m_actionError;

    qint64 m_duplicateSourceId = 0;
    QString m_duplicateName;
};

} // namespace hinkwise::presentation
