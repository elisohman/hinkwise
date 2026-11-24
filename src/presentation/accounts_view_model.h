// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dashboard_view_model.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/liability.h"
#include "domain/money.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace hinkwise::presentation {

/// One held asset as the buckets page reads it: what it is called, where the
/// model has placed it, what it is worth now and what was recorded for the
/// month on screen.
struct AccountRow {
    QString id;
    QString name;
    int kindIndex = 0;
    QString kindLabel;
    int bucketIndex = 0;
    QString bucketLabel;
    /// The most recent valuation, or a note that there is none.
    QString balanceLabel;
    /// The valuation recorded for the month the page names, or a note that it
    /// has not been recorded.
    QString monthLabel;
    bool hasMonthValue = false;
};

/// The accounts of one bucket as a QML list model, ordered by name.
class AccountRowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindIndexRole,
        KindLabelRole,
        BucketIndexRole,
        BucketLabelRole,
        BalanceLabelRole,
        MonthLabelRole,
        HasMonthValueRole,
    };

    explicit AccountRowModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<AccountRow> rows);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    QVector<AccountRow> m_rows;
};

/// One debt as the buckets page reads it. Debts carry no bucket: they reduce
/// the headline figure instead of filling one of the four.
struct DebtRow {
    QString id;
    QString name;
    int kindIndex = 0;
    QString kindLabel;
    QString rateLabel;
    int styleIndex = 0;
    /// The fields as a dialog reopens them: the rate without its unit and the
    /// term in years, both empty when the loan states nothing.
    QString rateText;
    QString paymentText;
    QString feeText;
    QString termYearsText;
    QString balanceLabel;
};

/// The household's debts as a QML list model, ordered by name.
class DebtRowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindIndexRole,
        KindLabelRole,
        RateLabelRole,
        StyleIndexRole,
        RateTextRole,
        PaymentTextRole,
        FeeTextRole,
        TermYearsTextRole,
        BalanceLabelRole,
    };

    explicit DebtRowModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<DebtRow> rows);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    QVector<DebtRow> m_rows;
};

/// The buckets page: the four buckets on the left, the accounts inside the
/// selected one on the right, and the debts underneath.
///
/// Every mutation goes through AppController, which routes it to the use case
/// and announces it, so the dashboard in the rail and the page that wrote can
/// never disagree. Amounts arrive from QML as the strings that were typed and
/// are read here; no arithmetic on money happens in QML.
class AccountsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int selectedBucket READ selectedBucket WRITE setSelectedBucket NOTIFY changed)
    Q_PROPERTY(QString selectedBucketLabel READ selectedBucketLabel NOTIFY changed)
    Q_PROPERTY(QString selectedBucketColourKey READ selectedBucketColourKey NOTIFY changed)
    Q_PROPERTY(QString bucketBalanceLabel READ bucketBalanceLabel NOTIFY changed)
    Q_PROPERTY(QString bucketTargetLabel READ bucketTargetLabel NOTIFY changed)
    Q_PROPERTY(QString monthLabel READ monthLabel NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)

    Q_PROPERTY(hinkwise::presentation::BucketsModel* buckets READ buckets CONSTANT)
    Q_PROPERTY(hinkwise::presentation::AccountRowModel* accounts READ accounts CONSTANT)
    Q_PROPERTY(hinkwise::presentation::DebtRowModel* debts READ debts CONSTANT)

    Q_PROPERTY(QStringList assetKindLabels READ assetKindLabels CONSTANT)
    Q_PROPERTY(QStringList liabilityKindLabels READ liabilityKindLabels CONSTANT)
    Q_PROPERTY(QStringList bucketLabels READ bucketLabels CONSTANT)
    /// The positions of the two kinds the dialogs explain, so the hint keys off
    /// the domain's enumeration rather than a number written in QML.
    Q_PROPERTY(int realEstateKindIndex READ realEstateKindIndex CONSTANT)
    Q_PROPERTY(int mortgageKindIndex READ mortgageKindIndex CONSTANT)
    /// The positions of the two repayment styles, so the dialog names the
    /// domain's enumeration instead of a number written in QML.
    Q_PROPERTY(int fixedAmortisationStyleIndex READ fixedAmortisationStyleIndex CONSTANT)
    Q_PROPERTY(int annuityStyleIndex READ annuityStyleIndex CONSTANT)

public:
    explicit AccountsViewModel(QObject* parent = nullptr);

    /// Rebuilds every row from the controller's current data.
    Q_INVOKABLE void reload();

    /// The bucket makeAsset() would place this kind in, so the dialog can show
    /// the model's answer before the household overrides it.
    Q_INVOKABLE int defaultBucketForKind(int kindIndex) const;

    /// The first kind whose home is this bucket. A new account starts from a
    /// kind that belongs where the household is looking, so adding one while
    /// the play bucket is selected does not put it in the buffer.
    Q_INVOKABLE int firstKindForBucket(int bucketIndex) const;

    /// The asset's latest balance as text a field can be reopened with, or an
    /// empty string when it has never been valued.
    Q_INVOKABLE QString latestBalanceText(const QString& assetId) const;

    // The mutations. Each returns the message to show on failure and an empty
    // string on success, which is what keeps a dialog open with its error
    // inline and closes it only once the write went through.
    Q_INVOKABLE QString addAccount(const QString& name, int kindIndex, int bucketIndex);
    Q_INVOKABLE QString updateAccount(const QString& assetId, const QString& name, int kindIndex,
                                      int bucketIndex);
    Q_INVOKABLE QString removeAccount(const QString& assetId);
    Q_INVOKABLE QString recordValuation(const QString& assetId, const QString& balanceText);
    /// Saves one balance per account in the selected bucket at once. `bytes` is
    /// keyed by asset id; a missing id is left alone. The month is one decision:
    /// every figure goes in inside one transaction, so a refusal leaves the whole
    /// month untouched rather than half recorded.
    Q_INVOKABLE QString recordMonthBalances(const QVariantMap& balances);
    /// Adds the debt and records the balance it starts with. The two writes are
    /// one decision, inside one transaction: a refused balance takes the loan
    /// with it instead of leaving a debt with no figure against it.
    Q_INVOKABLE QString addDebt(const QString& name, int kindIndex, const QString& rateText,
                                int styleIndex, const QString& paymentText, const QString& feeText,
                                const QString& termYearsText, const QString& balanceText);
    /// Changes a debt's own terms and leaves its id - and therefore every
    /// balance recorded against it - where it is. The balance is not part of
    /// the dialog: it is recorded through the update flow, not re-stated when a
    /// rate changes.
    Q_INVOKABLE QString updateDebt(const QString& liabilityId, const QString& name, int kindIndex,
                                   const QString& rateText, int styleIndex,
                                   const QString& paymentText, const QString& feeText,
                                   const QString& termYearsText);
    Q_INVOKABLE QString removeDebt(const QString& liabilityId);

    int selectedBucket() const;
    void setSelectedBucket(int index);

    QString selectedBucketLabel() const;
    QString selectedBucketColourKey() const;
    QString bucketBalanceLabel() const;
    QString bucketTargetLabel() const;
    QString monthLabel() const { return m_monthLabel; }
    QString currencyCode() const;

    BucketsModel* buckets() const { return m_buckets; }
    AccountRowModel* accounts() const { return m_accounts; }
    DebtRowModel* debts() const { return m_debts; }

    QStringList assetKindLabels() const;
    QStringList liabilityKindLabels() const;
    QStringList bucketLabels() const;

    int realEstateKindIndex() const { return static_cast<int>(domain::AssetKind::RealEstate); }
    int mortgageKindIndex() const { return static_cast<int>(domain::LiabilityKind::Mortgage); }
    int fixedAmortisationStyleIndex() const
    {
        return static_cast<int>(domain::RepaymentStyle::FixedAmortisation);
    }
    int annuityStyleIndex() const { return static_cast<int>(domain::RepaymentStyle::Annuity); }

signals:
    /// Anything the page renders changed. One signal: a write changes the rows,
    /// the bucket totals and the month label together.
    void changed();

private:
    domain::Bucket bucketAt(int index) const;
    domain::Currency currency() const { return m_currency; }
    /// Re-reads the rows for the selected bucket out of everything loaded.
    void applyFilter();

    int m_selectedBucket = 0;
    QString m_monthLabel;
    domain::Currency m_currency = domain::Currency::Sek;
    QVector<BucketRow> m_bucketRows;
    QVector<AccountRow> m_allAccounts;

    BucketsModel* m_buckets = nullptr;
    AccountRowModel* m_accounts = nullptr;
    DebtRowModel* m_debts = nullptr;
};

} // namespace hinkwise::presentation
