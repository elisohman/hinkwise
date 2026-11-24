// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/asset.h"
#include "domain/money.h"
#include "domain/profile.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace hinkwise::presentation {

/// One account as the setup wizard has it so far: a name, a kind, and the
/// balance the household typed.
///
/// The id is drawn once, when the row appears, so pressing Create a second time
/// after an error replaces the same asset instead of duplicating it. The bucket
/// is not a field: makeAsset() derives it from the kind, which is the model's
/// answer rather than the household's.
class AccountDraftModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        KindIndexRole,
        KindLabelRole,
        BucketLabelRole,
        BalanceTextRole,
        BalanceLabelRole,
        BalanceErrorRole,
    };

    explicit AccountDraftModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setName(int row, const QString& name);
    Q_INVOKABLE void setKind(int row, int kindIndex);
    Q_INVOKABLE void setBalanceText(int row, const QString& text);
    Q_INVOKABLE void addRow();
    Q_INVOKABLE void removeRow(int row);

    /// Re-reads every balance against `currency`. Called when the household
    /// changes currency on the first step, so no row is left holding an amount
    /// in a currency the profile will not have.
    void setCurrency(domain::Currency currency);

    /// True when there is at least one row, every row has a name, and every
    /// balance has been read as money.
    bool complete() const;

    /// What still has to be filled in, as one sentence, or empty when nothing
    /// does. This is what the step shows next to a disabled Next.
    QString firstProblem() const;

    /// The rows as assets, in the order they appear.
    std::vector<domain::Asset> toAssets() const;

    /// The balance of each row, parallel to toAssets().
    QVector<domain::Money> balances() const;

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    /// A row was added or removed.
    void countChanged();

private:
    struct Row {
        QString id;
        QString name;
        domain::AssetKind kind = domain::AssetKind::BankAccount;
        QString balanceText;
        QString balanceError;
        QString balanceLabel;
        domain::Money balance{};
        bool balanceOk = false;
    };

    void applyBalance(Row& row);
    void refreshRow(int row);
    int indexOf(const QModelIndex& index) const;

    QVector<Row> m_rows;
    domain::Currency m_currency = domain::Currency::Sek;
};

/// The five steps of setting up a household, as a screen sees them.
///
/// Every amount on these steps is a string in QML and a domain::Money here:
/// QML hands over what was typed, this class reads it with input_text.h and
/// reports the problem through the matching ...Error property. No amount is
/// ever added, divided or parsed in QML.
class WizardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool isReview READ isReview NOTIFY changed)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    Q_PROPERTY(QString blockingReason READ blockingReason NOTIFY changed)

    Q_PROPERTY(QVariantList currencyChoices READ currencyChoices CONSTANT)
    Q_PROPERTY(int currencyIndex READ currencyIndex WRITE setCurrencyIndex NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)

    Q_PROPERTY(QVariantList phaseChoices READ phaseChoices CONSTANT)
    Q_PROPERTY(int phaseIndex READ phaseIndex WRITE setPhaseIndex NOTIFY changed)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)

    /// The second question on the phase step: a life state and a way of
    /// keeping the record are both statements about how the household lives,
    /// and both decide how the application reads an old month.
    Q_PROPERTY(QVariantList trackingModeChoices READ trackingModeChoices CONSTANT)
    Q_PROPERTY(
        int trackingModeIndex READ trackingModeIndex WRITE setTrackingModeIndex NOTIFY changed)
    Q_PROPERTY(QString trackingModeLabel READ trackingModeLabel NOTIFY changed)

    Q_PROPERTY(QString incomeText READ incomeText WRITE setIncomeText NOTIFY changed)
    Q_PROPERTY(QString needsText READ needsText WRITE setNeedsText NOTIFY changed)
    Q_PROPERTY(QString wantsText READ wantsText WRITE setWantsText NOTIFY changed)
    Q_PROPERTY(QString incomeError READ incomeError NOTIFY changed)
    Q_PROPERTY(QString needsError READ needsError NOTIFY changed)
    Q_PROPERTY(QString wantsError READ wantsError NOTIFY changed)
    Q_PROPERTY(QString savingLabel READ savingLabel NOTIFY changed)
    Q_PROPERTY(QString savingNote READ savingNote NOTIFY changed)
    Q_PROPERTY(bool incomeValid READ incomeValid NOTIFY changed)

    Q_PROPERTY(hinkwise::presentation::AccountDraftModel* accounts READ accounts CONSTANT)
    Q_PROPERTY(QStringList assetKindLabels READ assetKindLabels CONSTANT)
    Q_PROPERTY(QVariantList reviewLines READ reviewLines NOTIFY changed)
    Q_PROPERTY(QString createError READ createError NOTIFY changed)

public:
    explicit WizardViewModel(QObject* parent = nullptr);

    int step() const { return m_step; }
    int stepCount() const;
    bool canGoBack() const { return m_step > 0; }
    bool canGoNext() const;
    bool isReview() const;
    bool canCreate() const;
    QString blockingReason() const;

    QVariantList currencyChoices() const;
    int currencyIndex() const { return m_currencyIndex; }
    QString currencyCode() const;

    QVariantList phaseChoices() const;
    int phaseIndex() const { return m_phaseIndex; }
    QString phaseLabel() const;

    QVariantList trackingModeChoices() const;
    int trackingModeIndex() const { return m_trackingModeIndex; }
    QString trackingModeLabel() const;

    QString incomeText() const { return m_income.text; }
    QString needsText() const { return m_needs.text; }
    QString wantsText() const { return m_wants.text; }
    QString incomeError() const { return m_income.error; }
    QString needsError() const { return m_needs.error; }
    QString wantsError() const { return m_wants.error; }
    QString savingLabel() const;
    QString savingNote() const;
    bool incomeValid() const;

    AccountDraftModel* accounts() const { return m_accounts; }
    QStringList assetKindLabels() const;
    QVariantList reviewLines() const;
    QString createError() const { return m_createError; }

    void setCurrencyIndex(int index);
    void setPhaseIndex(int index);
    void setTrackingModeIndex(int index);
    void setIncomeText(const QString& text);
    void setNeedsText(const QString& text);
    void setWantsText(const QString& text);

    /// Moves one step forward or back. next() does nothing while the current
    /// step is incomplete.
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();

    /// Creates the household exactly as the review step describes it: the
    /// profile, one asset per account, this month's flow and a valuation per
    /// account. Returns false and sets createError() when a use case refuses.
    Q_INVOKABLE bool createEconomy();

signals:
    /// Any state a step shows changed. One signal because a step that changes
    /// one field usually changes three, and nothing here is expensive.
    void changed();

private:
    /// A money field on one of the steps, kept as the text that was typed and
    /// the reading of it, so the error and the value can never disagree.
    struct Field {
        QString text;
        QString error;
        domain::Money value{};
        bool ok = false;
    };

    domain::Currency currency() const;
    domain::TrackingMode trackingMode() const;
    void applyCurrency();
    void applyField(Field& field, const QString& text);
    void refreshIncome();
    bool stepValid(int step) const;

    int m_step = 0;
    int m_currencyIndex = 0;
    int m_phaseIndex = 0;
    /// Budget planning by default, which is where a household that has not
    /// thought about it should start: it never turns a missed month into a
    /// warning on the first screen.
    int m_trackingModeIndex = static_cast<int>(domain::TrackingMode::Planning);
    Field m_income;
    Field m_needs;
    Field m_wants;
    AccountDraftModel* m_accounts = nullptr;
    QString m_createError;
};

} // namespace hinkwise::presentation
