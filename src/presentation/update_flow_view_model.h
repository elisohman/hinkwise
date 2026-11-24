// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/ports.h"
#include "cashflow_view_model.h"
#include "domain/money.h"
#include "domain/year_month.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace hinkwise::presentation {

/// One held account or one debt as the balances step reads it: what it is,
/// what the record says about it, and the amount the household has typed for
/// the month on screen.
///
/// `amountText` is the draft and `recordedText` is what that field started
/// from, which is what makes `dirty` mean "the household typed something"
/// rather than "the month has no figure yet".
struct BalanceEntryRow {
    QString id;
    QString name;
    QString detailLabel;
    bool isDebt = false;
    /// The most recent value on record and the month it was recorded for, or
    /// the note that the account has never been valued.
    QString recordLabel;
    QString amountText;
    QString recordedText;
    /// The per-row line under the field: the amount is the one the record
    /// already implies from an earlier month, or the field cannot be read.
    QString note;
    QString error;
    bool dirty = false;
    /// The amount in effect for the month on screen and the month it comes
    /// from. Empty when the account was never recorded that early. Not shown:
    /// the note above is what the household reads.
    std::optional<application::MoneyPoint> effect;
};

/// The accounts and debts as one editable list, which is what the balances
/// step is: every asset and every debt, in one pass, for one month.
///
/// The editing lives here rather than in QML so that the text a row holds, the
/// reading of it and the note under it cannot disagree, and so that saving is
/// one call with no map built in the interface.
class BalanceEntryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DetailLabelRole,
        IsDebtRole,
        RecordLabelRole,
        AmountTextRole,
        NoteRole,
        ErrorRole,
        DirtyRole,
    };

    explicit BalanceEntryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Replaces every row for `month`. A month change is a new list, not an
    /// edit of the old one, because every field starts from a different month's
    /// figures.
    void setRows(QVector<BalanceEntryRow> rows, domain::YearMonth month, domain::Currency currency);

    Q_INVOKABLE void setAmountText(int row, const QString& text);

    const QVector<BalanceEntryRow>& rows() const { return m_rows; }
    bool dirty() const;
    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();
    /// A draft changed, so the step's status and the leave-confirmation have to
    /// be told again.
    void edited();

private:
    void applyReading(BalanceEntryRow& row) const;

    QVector<BalanceEntryRow> m_rows;
    domain::YearMonth m_month{1970, 1};
    domain::Currency m_currency = domain::Currency::Sek;
    /// The month a save in this visit was last made for, so changing the month
    /// afterwards can say plainly that the new month is a different one.
    std::optional<domain::YearMonth> m_lastSavedMonth;
};

/// The recurring task, as one object: update the balances and the month's
/// cashflow, then look at what changed.
///
/// It owns two steps that save independently. Nothing is written until the
/// household presses the save for that step, and the status of one step is
/// never the status of the other: saving the balances says nothing about the
/// cashflow, and a step saved for one month is not saved for the next.
///
/// The month selector drives both steps, so an old month's balances and its
/// cashflow are entered in one pass. The month is never a future one: a month
/// that has not happened has no figures to enter.
class UpdateFlowViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int monthIndex READ monthIndex WRITE setMonthIndex NOTIFY changed)
    Q_PROPERTY(QStringList monthLabels READ monthLabels NOTIFY changed)
    Q_PROPERTY(QString monthLabel READ monthLabel NOTIFY changed)
    Q_PROPERTY(bool editingCurrentMonth READ editingCurrentMonth NOTIFY changed)
    /// What the chosen month means for the rest of the application, and what a
    /// save has just done to it. Empty when there is nothing to say.
    Q_PROPERTY(QString monthNote READ monthNote NOTIFY changed)
    Q_PROPERTY(QString resultNote READ resultNote NOTIFY changed)

    /// The action this flow is, named for the tracking mode, and the status of
    /// each step on its own. A step is never reported through the other one:
    /// saving the balances says nothing about the cashflow.
    Q_PROPERTY(QString actionLabel READ actionLabel NOTIFY changed)
    Q_PROPERTY(QString balancesStatus READ balancesStatus NOTIFY changed)
    Q_PROPERTY(QString cashflowStatus READ cashflowStatus NOTIFY changed)
    /// The untranslated handle the theme colours a status by: "saved",
    /// "unsaved" or "pending", the way severityKey names an insight.
    Q_PROPERTY(QString balancesStatusKey READ balancesStatusKey NOTIFY changed)
    Q_PROPERTY(QString cashflowStatusKey READ cashflowStatusKey NOTIFY changed)
    Q_PROPERTY(QString balancesError READ balancesError NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)

    Q_PROPERTY(hinkwise::presentation::BalanceEntryModel* balances READ balances CONSTANT)
    Q_PROPERTY(hinkwise::presentation::CashflowViewModel* cashflow READ cashflow CONSTANT)

public:
    explicit UpdateFlowViewModel(QObject* parent = nullptr);

    /// Starts the flow: the current month, no saved steps, every row read
    /// afresh. Called on every open, so nothing typed in a previous visit is
    /// left behind.
    Q_INVOKABLE void begin();

    /// Reads the rows again for the month on screen, keeping nothing typed.
    void reload();

    /// Writes every non-empty balance for the month on screen. Returns the
    /// message to show when a field cannot be read or the write is refused.
    /// The month is written whole or not at all: every row goes in inside one
    /// transaction, which is dropped - rolling the month back - on the first row
    /// that fails.
    Q_INVOKABLE QString saveBalances();

    /// Writes the cashflow step for the month on screen. Returns the message
    /// from the cashflow editor, which is the one the page shows.
    Q_INVOKABLE QString saveCashflow();

    int monthIndex() const { return m_monthIndex; }
    QStringList monthLabels() const { return m_monthLabels; }
    QString monthLabel() const;
    bool editingCurrentMonth() const;
    QString monthNote() const { return m_monthNote; }
    QString resultNote() const { return m_resultNote; }

    QString actionLabel() const { return m_actionLabel; }
    QString balancesStatus() const;
    QString cashflowStatus() const;
    QString balancesStatusKey() const;
    QString cashflowStatusKey() const;
    QString balancesError() const { return m_balancesError; }
    bool dirty() const;
    bool hasAccounts() const { return m_hasAccounts; }
    QString currencyCode() const;

    BalanceEntryModel* balances() const { return m_balances; }
    CashflowViewModel* cashflow() const { return m_cashflow; }

    void setMonthIndex(int index);

signals:
    void changed();

private:
    /// The month the two steps write to.
    domain::YearMonth month() const;
    void rebuildMonthLabels();
    void rebuildRows();
    /// Records what a save of a past month did to the rest of the screen, so
    /// the household is never left thinking an old month became the current
    /// one.
    void noteSaveResult(domain::YearMonth savedMonth);

    int m_monthIndex = 0;
    QStringList m_monthLabels;
    bool m_balancesSavedForMonth = false;
    bool m_cashflowSavedForMonth = false;
    QString m_balancesError;
    QString m_actionLabel;
    QString m_monthNote;
    QString m_resultNote;
    bool m_hasAccounts = false;
    domain::Currency m_currency = domain::Currency::Sek;
    /// The month a save in this visit was last made for, so changing the month
    /// afterwards can say plainly that the new month is a different one.
    std::optional<domain::YearMonth> m_lastSavedMonth;

    BalanceEntryModel* m_balances = nullptr;
    CashflowViewModel* m_cashflow = nullptr;
};

} // namespace hinkwise::presentation
