// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "cashflow_series.h"
#include "domain/money.h"
#include "domain/year_month.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace hinkwise::presentation {

/// One column of the twelve-month history strip: what month it is, the split of
/// that month's income as 0..1 positions, and whether the month has a flow at
/// all. An unrecorded column is not a column of zeros; the page draws it empty.
struct HistoryColumn {
    QString label;
    QString fullLabel;
    bool recorded = false;
    double needsRatio = 0.0;
    double wantsRatio = 0.0;
    double savingRatio = 0.0;
    bool selected = false;
};

/// The twelve columns of the history strip, oldest first.
class HistoryColumnsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        FullLabelRole,
        RecordedRole,
        NeedsRatioRole,
        WantsRatioRole,
        SavingRatioRole,
        SelectedRole,
    };

    explicit HistoryColumnsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<HistoryColumn> rows);
    void setSelected(int row);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    QVector<HistoryColumn> m_rows;
};

/// The cashflow page: the model's 50/30/20 split of one month, the twelve
/// months behind it, and the savings rate over the same window.
///
/// The split is read from the domain's metrics, so the deviation the page
/// states in words is the one the rules act on. The history comes from the
/// recorded month flows: a month the household never recorded is reported as
/// absent, never as a zero, and the page says so instead of drawing a bar.
///
/// The amounts are strings in QML and domain::Money here. This class reads what
/// was typed, writes it through AppController's recordMonth() use case, and
/// hands back the sentence to show when the write cannot happen.
class CashflowViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// How many months the history strip and the savings line cover.
    Q_PROPERTY(int historyMonths READ historyMonths CONSTANT)

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)
    Q_PROPERTY(QString currentMonthLabel READ currentMonthLabel NOTIFY changed)
    Q_PROPERTY(QString selectedMonthLabel READ selectedMonthLabel NOTIFY changed)
    /// The month the form writes to. On this page that is always the current
    /// month; the update flow pins it to whatever month the household chose to
    /// record, which is how one editor serves both.
    Q_PROPERTY(QString editingMonthLabel READ editingMonthLabel NOTIFY changed)
    /// True when a flow is on record for the month the form writes to. False is
    /// not a month of zeros.
    Q_PROPERTY(bool editingMonthRecorded READ editingMonthRecorded NOTIFY changed)

    Q_PROPERTY(QString incomeText READ incomeText WRITE setIncomeText NOTIFY changed)
    Q_PROPERTY(QString needsText READ needsText WRITE setNeedsText NOTIFY changed)
    Q_PROPERTY(QString wantsText READ wantsText WRITE setWantsText NOTIFY changed)
    Q_PROPERTY(QString savingText READ savingText WRITE setSavingText NOTIFY changed)
    Q_PROPERTY(QString bufferSplitText READ bufferSplitText WRITE setBufferSplitText NOTIFY changed)
    Q_PROPERTY(QString investingSplitText READ investingSplitText WRITE setInvestingSplitText NOTIFY
                   changed)
    Q_PROPERTY(QString amortisationSplitText READ amortisationSplitText WRITE
                   setAmortisationSplitText NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool savingSplitValid READ savingSplitValid NOTIFY changed)
    Q_PROPERTY(QString savingSplitError READ savingSplitError NOTIFY changed)
    Q_PROPERTY(bool savingSplitCanFill READ savingSplitCanFill NOTIFY changed)
    Q_PROPERTY(QString saveError READ saveError NOTIFY changed)
    Q_PROPERTY(QString savedNote READ savedNote NOTIFY changed)
    Q_PROPERTY(QString savingNote READ savingNote NOTIFY changed)

    Q_PROPERTY(QString incomeLabel READ incomeLabel NOTIFY changed)
    Q_PROPERTY(QString needsLabel READ needsLabel NOTIFY changed)
    Q_PROPERTY(QString wantsLabel READ wantsLabel NOTIFY changed)
    Q_PROPERTY(QString savingLabel READ savingLabel NOTIFY changed)
    Q_PROPERTY(QString needsShareLabel READ needsShareLabel NOTIFY changed)
    Q_PROPERTY(QString wantsShareLabel READ wantsShareLabel NOTIFY changed)
    Q_PROPERTY(QString savingShareLabel READ savingShareLabel NOTIFY changed)
    Q_PROPERTY(QString needsTargetLabel READ needsTargetLabel NOTIFY changed)
    Q_PROPERTY(QString wantsTargetLabel READ wantsTargetLabel NOTIFY changed)
    Q_PROPERTY(QString savingTargetLabel READ savingTargetLabel NOTIFY changed)
    Q_PROPERTY(QString needsDeviationLabel READ needsDeviationLabel NOTIFY changed)
    Q_PROPERTY(QString wantsDeviationLabel READ wantsDeviationLabel NOTIFY changed)
    Q_PROPERTY(QString savingDeviationLabel READ savingDeviationLabel NOTIFY changed)
    Q_PROPERTY(double needsRatio READ needsRatio NOTIFY changed)
    Q_PROPERTY(double wantsRatio READ wantsRatio NOTIFY changed)
    Q_PROPERTY(double savingRatio READ savingRatio NOTIFY changed)
    Q_PROPERTY(bool selectedMonthRecorded READ selectedMonthRecorded NOTIFY changed)
    Q_PROPERTY(QString selectedSavingsRateLabel READ selectedSavingsRateLabel NOTIFY changed)
    Q_PROPERTY(bool selectedMonthHasSavingSplit READ selectedMonthHasSavingSplit NOTIFY changed)
    Q_PROPERTY(QString bufferSavingShareLabel READ bufferSavingShareLabel NOTIFY changed)
    Q_PROPERTY(QString investingSavingShareLabel READ investingSavingShareLabel NOTIFY changed)
    Q_PROPERTY(
        QString amortisationSavingShareLabel READ amortisationSavingShareLabel NOTIFY changed)
    Q_PROPERTY(QString bufferSavingAmountLabel READ bufferSavingAmountLabel NOTIFY changed)
    Q_PROPERTY(QString investingSavingAmountLabel READ investingSavingAmountLabel NOTIFY changed)
    Q_PROPERTY(
        QString amortisationSavingAmountLabel READ amortisationSavingAmountLabel NOTIFY changed)

    Q_PROPERTY(QVariantList savingsPoints READ savingsPoints NOTIFY changed)
    Q_PROPERTY(QVariantList savingsReferences READ savingsReferences NOTIFY changed)
    Q_PROPERTY(QString savingsAxisTopLabel READ savingsAxisTopLabel NOTIFY changed)
    Q_PROPERTY(QString savingsFirstLabel READ savingsFirstLabel NOTIFY changed)
    Q_PROPERTY(QString savingsLastLabel READ savingsLastLabel NOTIFY changed)
    Q_PROPERTY(bool savingsEmpty READ savingsEmpty NOTIFY changed)

    Q_PROPERTY(hinkwise::presentation::HistoryColumnsModel* history READ history CONSTANT)

public:
    explicit CashflowViewModel(QObject* parent = nullptr);

    /// Rebuilds every figure from the controller's current data, keeping the
    /// month the household selected when it is still inside the window.
    Q_INVOKABLE void reload();

    /// Selects one column of the history strip, oldest first.
    Q_INVOKABLE void selectMonth(int index);

    /// Records the current month from the draft the page edits. An empty field
    /// means zero, and the whole month is refused when income cannot cover what
    /// is entered, or when a saving breakdown is present but does not add up.
    Q_INVOKABLE QString saveMonth();

    /// Puts the saving total that is not already broken down into the
    /// investments field. A no-op when that field is no longer empty or when
    /// the amounts the household has entered already exceed the total.
    Q_INVOKABLE void fillSavingSplitRemainder();

    /// Restores every draft field to the last values loaded from the store.
    Q_INVOKABLE void discardMonth();

    int historyMonths() const { return kHistoryMonths; }
    bool hasProfile() const { return m_hasProfile; }
    QString currencyCode() const;
    QString currentMonthLabel() const { return m_currentMonthLabel; }
    QString selectedMonthLabel() const { return m_selectedMonthLabel; }
    QString editingMonthLabel() const { return m_editingMonthLabel; }
    bool editingMonthRecorded() const { return m_editingRecorded; }

    /// Points the form at `month`. Nothing else on the page moves: the history
    /// strip and its selection are about reading, the form is about writing.
    void setEditingMonth(domain::YearMonth month);
    QString incomeText() const { return m_incomeText; }
    QString needsText() const { return m_needsText; }
    QString wantsText() const { return m_wantsText; }
    QString savingText() const { return m_savingText; }
    QString bufferSplitText() const { return m_bufferSplitText; }
    QString investingSplitText() const { return m_investingSplitText; }
    QString amortisationSplitText() const { return m_amortisationSplitText; }
    bool dirty() const;
    bool savingSplitValid() const;
    QString savingSplitError() const;
    bool savingSplitCanFill() const;
    QString saveError() const { return m_saveError; }
    QString savedNote() const { return m_savedNote; }
    QString savingNote() const { return m_savingNote; }
    QString incomeLabel() const { return m_incomeLabel; }
    QString needsLabel() const { return m_needsLabel; }
    QString wantsLabel() const { return m_wantsLabel; }
    QString savingLabel() const { return m_savingLabel; }
    QString needsShareLabel() const { return m_needsShareLabel; }
    QString wantsShareLabel() const { return m_wantsShareLabel; }
    QString savingShareLabel() const { return m_savingShareLabel; }
    QString needsTargetLabel() const { return m_needsTargetLabel; }
    QString wantsTargetLabel() const { return m_wantsTargetLabel; }
    QString savingTargetLabel() const { return m_savingTargetLabel; }
    QString needsDeviationLabel() const { return m_needsDeviationLabel; }
    QString wantsDeviationLabel() const { return m_wantsDeviationLabel; }
    QString savingDeviationLabel() const { return m_savingDeviationLabel; }
    double needsRatio() const { return m_split.needs.asFraction(); }
    double wantsRatio() const { return m_split.wants.asFraction(); }
    double savingRatio() const { return m_split.saving.asFraction(); }
    bool selectedMonthRecorded() const { return m_selectedRecorded; }
    QString selectedSavingsRateLabel() const { return m_selectedSavingsRateLabel; }
    bool selectedMonthHasSavingSplit() const { return m_selectedHasSavingSplit; }
    QString bufferSavingShareLabel() const { return m_bufferSavingShareLabel; }
    QString investingSavingShareLabel() const { return m_investingSavingShareLabel; }
    QString amortisationSavingShareLabel() const { return m_amortisationSavingShareLabel; }
    QString bufferSavingAmountLabel() const { return m_bufferSavingAmountLabel; }
    QString investingSavingAmountLabel() const { return m_investingSavingAmountLabel; }
    QString amortisationSavingAmountLabel() const { return m_amortisationSavingAmountLabel; }
    QVariantList savingsPoints() const { return m_savingsPoints; }
    QVariantList savingsReferences() const { return m_savingsReferences; }
    QString savingsAxisTopLabel() const { return m_savingsAxisTopLabel; }
    QString savingsFirstLabel() const { return m_savingsFirstLabel; }
    QString savingsLastLabel() const { return m_savingsLastLabel; }
    bool savingsEmpty() const { return m_savingsEmpty; }
    HistoryColumnsModel* history() const { return m_history; }

    void setIncomeText(const QString& text);
    void setNeedsText(const QString& text);
    void setWantsText(const QString& text);
    void setSavingText(const QString& text);
    void setBufferSplitText(const QString& text);
    void setInvestingSplitText(const QString& text);
    void setAmortisationSplitText(const QString& text);

signals:
    void changed();

private:
    static constexpr int kHistoryMonths = 12;

    /// Rebuilds from the controller, keeping the draft when `keepDraft` is set.
    /// The stored baseline always moves to what is on record, so Discard
    /// restores the newest saved values even after a reload arrived mid-edit.
    void reloadFromStore(bool keepDraft);
    void applySelection(const QVector<FlowMonth>& months);
    QString monthLabel(domain::YearMonth month, const QString& format) const;
    bool anySavingSplitText() const;

    bool m_hasProfile = false;
    domain::Currency m_currency = domain::Currency::Sek;
    domain::YearMonth m_selectedMonth{1970, 1};
    domain::YearMonth m_editingMonth{1970, 1};
    /// False while the form follows the calendar, which is what makes the page
    /// name the new month when one rolls over.
    bool m_editingPinned = false;

    QString m_currentMonthLabel;
    QString m_selectedMonthLabel;
    QString m_editingMonthLabel;
    QString m_incomeText;
    QString m_needsText;
    QString m_wantsText;
    QString m_savingText;
    QString m_bufferSplitText;
    QString m_investingSplitText;
    QString m_amortisationSplitText;
    QString m_storedIncomeText;
    QString m_storedNeedsText;
    QString m_storedWantsText;
    QString m_storedSavingText;
    QString m_storedBufferSplitText;
    QString m_storedInvestingSplitText;
    QString m_storedAmortisationSplitText;
    QString m_saveError;
    QString m_savedNote;
    QString m_savingNote;
    QString m_incomeLabel;
    QString m_needsLabel;
    QString m_wantsLabel;
    QString m_savingLabel;
    QString m_needsShareLabel;
    QString m_wantsShareLabel;
    QString m_savingShareLabel;
    QString m_needsTargetLabel;
    QString m_wantsTargetLabel;
    QString m_savingTargetLabel;
    QString m_needsDeviationLabel;
    QString m_wantsDeviationLabel;
    QString m_savingDeviationLabel;
    QString m_selectedSavingsRateLabel;
    QString m_bufferSavingShareLabel;
    QString m_investingSavingShareLabel;
    QString m_amortisationSavingShareLabel;
    QString m_bufferSavingAmountLabel;
    QString m_investingSavingAmountLabel;
    QString m_amortisationSavingAmountLabel;
    QString m_savingsAxisTopLabel;
    QString m_savingsFirstLabel;
    QString m_savingsLastLabel;
    bool m_selectedRecorded = false;
    bool m_editingRecorded = false;
    bool m_selectedHasSavingSplit = false;
    bool m_savingsEmpty = true;
    IncomeSplit m_split;
    QVariantList m_savingsPoints;
    QVariantList m_savingsReferences;

    HistoryColumnsModel* m_history = nullptr;
};

} // namespace hinkwise::presentation
