// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"
#include "domain/jurisdiction.h"
#include "domain/phase.h"
#include "domain/profile.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace hinkwise::presentation {

/// One line of the before/after a phase change would produce: what the figure
/// is, what it is now, and what it becomes.
///
/// Rows the phase does not change are listed too, with changed false. The sheet
/// exists to say what stays put - the Play cap that survives every phase, for
/// one - as much as what moves, so it never shows only half of the difference.
struct PhaseComparisonRow {
    QString label;
    QString before;
    QString after;
    bool changed = true;
};

/// The before/after table for one household, derived from the domain's own
/// sizing tables rather than written out by hand: the buffer and preserve
/// bands, the gap against the buffer balance and the flow direction all come
/// from `policyFor()` and `resolve()`, so the sheet cannot describe a phase the
/// model does not implement.
///
/// A free function so the settings page and its test read the same rows.
QVector<PhaseComparisonRow> phaseComparisonOf(const application::DashboardSnapshot& snapshot,
                                              domain::Phase next);

/// The settings page: the phase switcher, the jurisdiction, the household's
/// name, where its data lives, and the About facts.
///
/// Changing phase is not an edit of a field: it goes through the changePhase
/// use case, which appends the history entry as well as saving, and the sheet
/// that precedes it is built from phaseComparisonOf() so the household sees the
/// new targets in its own money before anything is written.
class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)

    Q_PROPERTY(QVariantList phaseChoices READ phaseChoices CONSTANT)
    Q_PROPERTY(int phaseIndex READ phaseIndex NOTIFY changed)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
    Q_PROPERTY(int pendingPhaseIndex READ pendingPhaseIndex NOTIFY changed)
    Q_PROPERTY(QString pendingPhaseLabel READ pendingPhaseLabel NOTIFY changed)
    Q_PROPERTY(bool comparisonVisible READ comparisonVisible NOTIFY changed)
    Q_PROPERTY(QVariantList comparisonRows READ comparisonRows NOTIFY changed)
    Q_PROPERTY(bool comparisonChanges READ comparisonChanges NOTIFY changed)
    Q_PROPERTY(QString phaseError READ phaseError NOTIFY changed)

    Q_PROPERTY(QString displayNameText READ displayNameText WRITE setDisplayNameText NOTIFY changed)
    Q_PROPERTY(QString displayNameError READ displayNameError NOTIFY changed)
    Q_PROPERTY(bool displayNameDirty READ displayNameDirty NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)
    Q_PROPERTY(QString currencyName READ currencyName NOTIFY changed)

    /// Where the household wants to sit inside the buffer band, in basis
    /// points: 0 is the cautious end, 10000 the bold end. The band itself is
    /// the phase's; this is the point inside it.
    Q_PROPERTY(int bufferTargetPosition READ bufferTargetPosition WRITE setBufferTargetPosition
                   NOTIFY changed)
    Q_PROPERTY(int bufferTargetMaximum READ bufferTargetMaximum CONSTANT)
    Q_PROPERTY(bool bufferTargetDirty READ bufferTargetDirty NOTIFY changed)
    Q_PROPERTY(QString bufferTargetPreview READ bufferTargetPreview NOTIFY changed)
    Q_PROPERTY(QString bufferTargetError READ bufferTargetError NOTIFY changed)

    Q_PROPERTY(QString safeWithdrawalRateText READ safeWithdrawalRateText WRITE
                   setSafeWithdrawalRateText NOTIFY changed)
    Q_PROPERTY(QString capitalTaxDragText READ capitalTaxDragText WRITE setCapitalTaxDragText NOTIFY
                   changed)
    Q_PROPERTY(QString fiMultiplierLowText READ fiMultiplierLowText WRITE setFiMultiplierLowText
                   NOTIFY changed)
    Q_PROPERTY(QString fiMultiplierDefaultText READ fiMultiplierDefaultText WRITE
                   setFiMultiplierDefaultText NOTIFY changed)
    Q_PROPERTY(QString fiMultiplierHighText READ fiMultiplierHighText WRITE setFiMultiplierHighText
                   NOTIFY changed)
    Q_PROPERTY(QString safeWithdrawalRateError READ safeWithdrawalRateError NOTIFY changed)
    Q_PROPERTY(QString capitalTaxDragError READ capitalTaxDragError NOTIFY changed)
    Q_PROPERTY(QString multiplierError READ multiplierError NOTIFY changed)
    Q_PROPERTY(bool jurisdictionDirty READ jurisdictionDirty NOTIFY changed)

    Q_PROPERTY(QString databasePath READ databasePath NOTIFY changed)
    Q_PROPERTY(QUrl backupFileUrl READ backupFileUrl NOTIFY changed)
    Q_PROPERTY(QString backupMessage READ backupMessage NOTIFY changed)
    Q_PROPERTY(QString backupError READ backupError NOTIFY changed)

    Q_PROPERTY(QVariantList phaseHistory READ phaseHistory NOTIFY changed)

    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)

public:
    explicit SettingsViewModel(QObject* parent = nullptr);

    /// Rebuilds everything from the controller's current data. Called at
    /// construction and after every write, so a phase change made here and a
    /// month recorded on another page agree about what the profile says.
    Q_INVOKABLE void reload();

    /// Opens the before/after for `index`, without writing anything. Nothing
    /// changes until confirmPhaseChange() is called.
    Q_INVOKABLE void beginPhaseChange(int index);

    /// Applies the pending phase through the changePhase use case. Returns the
    /// reason it could not, or empty when the household has moved.
    Q_INVOKABLE QString confirmPhaseChange();

    /// Closes the sheet and forgets the pending phase.
    Q_INVOKABLE void cancelPhaseChange();

    /// Saves the name the page collected. Returns the reason it could not.
    Q_INVOKABLE QString saveDisplayName();

    /// Restores the name field to the stored name.
    Q_INVOKABLE void discardDisplayName();

    /// Saves the jurisdiction the page collected and recomputes every figure,
    /// which AppController does by announcing the write. Returns the reason it
    /// could not.
    Q_INVOKABLE QString saveJurisdiction();

    /// Restores the jurisdiction fields to the stored assumptions.
    Q_INVOKABLE void discardJurisdiction();

    /// Saves the chosen buffer point through the same profile-save path as the
    /// other settings, which recomputes every target. Returns the reason it
    /// could not.
    Q_INVOKABLE QString saveBufferTarget();

    /// Restores the buffer point to the stored one.
    Q_INVOKABLE void discardBufferTarget();

    /// Fills the jurisdiction fields with the Swedish constants. Nothing is
    /// written until saveJurisdiction() is called, so a household that resets
    /// by mistake can still cancel by leaving the page.
    Q_INVOKABLE void resetJurisdictionToSwedishDefaults();

    /// Writes the JSON backup to `fileUrl`. Returns the reason it could not,
    /// and leaves backupMessage() describing the file that was written.
    Q_INVOKABLE QString exportBackup(const QUrl& fileUrl);

    bool hasProfile() const { return m_hasProfile; }

    QVariantList phaseChoices() const;
    int phaseIndex() const { return m_phaseIndex; }
    QString phaseLabel() const;
    int pendingPhaseIndex() const { return m_pendingPhaseIndex; }
    QString pendingPhaseLabel() const;
    bool comparisonVisible() const { return m_pendingPhaseIndex >= 0; }
    QVariantList comparisonRows() const;
    bool comparisonChanges() const;
    QString phaseError() const { return m_phaseError; }

    QString displayNameText() const { return m_displayNameText; }
    QString displayNameError() const { return m_displayNameError; }
    bool displayNameDirty() const;
    QString currencyCode() const;
    QString currencyName() const;

    int bufferTargetPosition() const { return m_bufferTargetPosition; }
    int bufferTargetMaximum() const { return 10000; }
    bool bufferTargetDirty() const;
    QString bufferTargetPreview() const;
    QString bufferTargetError() const { return m_bufferTargetError; }

    QString safeWithdrawalRateText() const { return m_safeWithdrawalRateText; }
    QString capitalTaxDragText() const { return m_capitalTaxDragText; }
    QString fiMultiplierLowText() const { return m_fiMultiplierLowText; }
    QString fiMultiplierDefaultText() const { return m_fiMultiplierDefaultText; }
    QString fiMultiplierHighText() const { return m_fiMultiplierHighText; }
    QString safeWithdrawalRateError() const { return m_safeWithdrawalRateError; }
    QString capitalTaxDragError() const { return m_capitalTaxDragError; }
    QString multiplierError() const { return m_multiplierError; }
    bool jurisdictionDirty() const;

    QString databasePath() const;
    QUrl backupFileUrl() const;
    QString backupMessage() const { return m_backupMessage; }
    QString backupError() const { return m_backupError; }

    QVariantList phaseHistory() const { return m_phaseHistory; }

    QString appVersion() const;
    QString qtVersion() const;

    void setDisplayNameText(const QString& text);
    void setBufferTargetPosition(int position);
    void setSafeWithdrawalRateText(const QString& text);
    void setCapitalTaxDragText(const QString& text);
    void setFiMultiplierLowText(const QString& text);
    void setFiMultiplierDefaultText(const QString& text);
    void setFiMultiplierHighText(const QString& text);

signals:
    /// Anything the page shows changed. One signal because a write here
    /// usually changes several lines, and nothing on this page is expensive.
    void changed();

private:
    void apply(const application::DashboardSnapshot& snapshot);

    /// The jurisdiction as the rest of the application reads it: a profile
    /// saved before the jurisdiction was ever edited carries no id, and the
    /// dashboard falls back to the Swedish constants for it, so the fields have
    /// to show those same constants.
    domain::Jurisdiction jurisdiction() const;
    void fillJurisdictionTexts();

    QVariantList phaseHistoryRows() const;

    bool m_hasProfile = false;
    application::DashboardSnapshot m_snapshot;
    domain::Profile m_profile;

    int m_phaseIndex = 0;
    int m_pendingPhaseIndex = -1;
    QString m_phaseError;

    QString m_displayNameText;
    QString m_displayNameError;

    int m_bufferTargetPosition = 5000;
    QString m_bufferTargetError;

    /// False until the jurisdiction fields have been shown once, so the first
    /// reload fills them even when the stored values are the Swedish defaults.
    bool m_jurisdictionFilled = false;

    QString m_safeWithdrawalRateText;
    QString m_capitalTaxDragText;
    QString m_fiMultiplierLowText;
    QString m_fiMultiplierDefaultText;
    QString m_fiMultiplierHighText;
    QString m_safeWithdrawalRateError;
    QString m_capitalTaxDragError;
    QString m_multiplierError;

    QVariantList m_phaseHistory;
    QString m_backupMessage;
    QString m_backupError;
};

} // namespace hinkwise::presentation
