// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dashboard_view_model.h"
#include "domain/money.h"
#include "domain/planned_expense.h"
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

namespace hinkwise::presentation {

/// One planned purchase as the review lists it.
struct GoalRow {
    QString id;
    QString name;
    QString amountLabel;
    QString dueLabel;
    /// True when the purchase is due inside the three years the buffer target
    /// counts, which is what makes it change that target.
    bool counted = false;
};

/// The household's planned purchases, soonest first.
class GoalRowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        AmountLabelRole,
        DueLabelRole,
        CountedRole,
    };

    explicit GoalRowModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<GoalRow> rows);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    QVector<GoalRow> m_rows;
};

/// One movement a rebalance proposal suggests: from a bucket, to a bucket, and
/// how much. The colours travel with it so the page does not have to look up
/// which key belongs to which label.
struct TransferRow {
    QString fromLabel;
    QString toLabel;
    QString fromColourKey;
    QString toColourKey;
    QString amountLabel;
};

/// The transfers of one proposal, in the order the model would make them.
class TransferRowModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        FromLabelRole = Qt::UserRole + 1,
        ToLabelRole,
        FromColourKeyRole,
        ToColourKeyRole,
        AmountLabelRole,
    };

    explicit TransferRowModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<TransferRow> rows);

    int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();

private:
    QVector<TransferRow> m_rows;
};

/// The year-end review: the annual ritual the model describes, in four steps,
/// ending in the proposal domain::planRebalance() makes.
///
/// Nothing on this page moves money. Every figure is read, every suggestion is
/// the domain's own words, and the one switch - market down - only changes what
/// the planner is told, so a household can see why the plan changes when the
/// market does.
class ReviewViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)

    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
    Q_PROPERTY(QString phaseNote READ phaseNote NOTIFY changed)
    Q_PROPERTY(bool hasFlow READ hasFlow NOTIFY changed)
    Q_PROPERTY(QString incomeLabel READ incomeLabel NOTIFY changed)
    Q_PROPERTY(QString expensesLabel READ expensesLabel NOTIFY changed)
    Q_PROPERTY(QString flowNote READ flowNote NOTIFY changed)

    Q_PROPERTY(QStringList monthLabels READ monthLabels CONSTANT)
    Q_PROPERTY(QStringList dueYearLabels READ dueYearLabels NOTIFY changed)
    /// Where the "when" picker starts: this month and this year, so a purchase
    /// is not dated in the past by default.
    Q_PROPERTY(int dueMonthDefault READ dueMonthDefault NOTIFY changed)
    Q_PROPERTY(int dueYearDefault READ dueYearDefault CONSTANT)
    Q_PROPERTY(QString lumpSumLabel READ lumpSumLabel NOTIFY changed)
    Q_PROPERTY(QString bufferTargetNowLabel READ bufferTargetNowLabel NOTIFY changed)
    Q_PROPERTY(QString bufferTargetWithGoalsLabel READ bufferTargetWithGoalsLabel NOTIFY changed)
    Q_PROPERTY(QString goalsEffectLabel READ goalsEffectLabel NOTIFY changed)
    Q_PROPERTY(bool goalsBeyondThreeYears READ goalsBeyondThreeYears NOTIFY changed)

    Q_PROPERTY(bool marketDown READ marketDown WRITE setMarketDown NOTIFY changed)
    /// False in the accumulating phases, where the model never sells growth, so
    /// the switch cannot change the plan until the phase does.
    Q_PROPERTY(bool marketDownApplies READ marketDownApplies NOTIFY changed)
    Q_PROPERTY(QString nextContributionLabel READ nextContributionLabel NOTIFY changed)
    Q_PROPERTY(bool hasNextContribution READ hasNextContribution NOTIFY changed)
    Q_PROPERTY(QString rationaleLabel READ rationaleLabel NOTIFY changed)
    Q_PROPERTY(bool proposalEmpty READ proposalEmpty NOTIFY changed)

    Q_PROPERTY(hinkwise::presentation::GoalRowModel* goals READ goals CONSTANT)
    Q_PROPERTY(hinkwise::presentation::TransferRowModel* transfers READ transfers CONSTANT)
    Q_PROPERTY(hinkwise::presentation::BucketsModel* buckets READ buckets CONSTANT)

public:
    explicit ReviewViewModel(QObject* parent = nullptr);

    /// Rebuilds every step from the controller's current data.
    Q_INVOKABLE void reload();

    /// Adds a planned purchase. `monthIndex` is 0 for January; `yearIndex` is
    /// a position in dueYearLabels(), so the page never builds a date itself.
    Q_INVOKABLE QString addGoal(const QString& name, const QString& amountText, int monthIndex,
                                int yearIndex);
    Q_INVOKABLE QString removeGoal(const QString& goalId);

    bool hasProfile() const { return m_hasProfile; }
    QString currencyCode() const;
    QString phaseLabel() const { return m_phaseLabel; }
    QString phaseNote() const { return m_phaseNote; }
    bool hasFlow() const { return m_hasFlow; }
    QString incomeLabel() const { return m_incomeLabel; }
    QString expensesLabel() const { return m_expensesLabel; }
    QString flowNote() const { return m_flowNote; }
    QStringList monthLabels() const;
    QStringList dueYearLabels() const { return m_dueYearLabels; }
    int dueMonthDefault() const;
    int dueYearDefault() const;
    QString lumpSumLabel() const { return m_lumpSumLabel; }
    QString bufferTargetNowLabel() const { return m_bufferTargetNowLabel; }
    QString bufferTargetWithGoalsLabel() const { return m_bufferTargetWithGoalsLabel; }
    QString goalsEffectLabel() const { return m_goalsEffectLabel; }
    bool goalsBeyondThreeYears() const { return m_goalsBeyondThreeYears; }
    bool marketDown() const { return m_marketDown; }
    bool marketDownApplies() const { return m_marketDownApplies; }
    void setMarketDown(bool marketDown);
    QString nextContributionLabel() const { return m_nextContributionLabel; }
    bool hasNextContribution() const { return m_hasNextContribution; }
    QString rationaleLabel() const { return m_rationaleLabel; }
    bool proposalEmpty() const { return m_transfers->count() == 0; }
    GoalRowModel* goals() const { return m_goals; }
    TransferRowModel* transfers() const { return m_transfers; }
    BucketsModel* buckets() const { return m_buckets; }

signals:
    void changed();

private:
    void apply(const application::DashboardSnapshot& snapshot);

    bool m_hasProfile = false;
    domain::Currency m_currency = domain::Currency::Sek;
    QString m_phaseLabel;
    QString m_phaseNote;
    bool m_hasFlow = false;
    QString m_incomeLabel;
    QString m_expensesLabel;
    QString m_flowNote;
    QStringList m_dueYearLabels;
    QString m_lumpSumLabel;
    QString m_bufferTargetNowLabel;
    QString m_bufferTargetWithGoalsLabel;
    QString m_goalsEffectLabel;
    bool m_goalsBeyondThreeYears = false;
    bool m_marketDown = false;
    bool m_marketDownApplies = true;
    QString m_nextContributionLabel;
    bool m_hasNextContribution = false;
    QString m_rationaleLabel;

    GoalRowModel* m_goals = nullptr;
    TransferRowModel* m_transfers = nullptr;
    BucketsModel* m_buckets = nullptr;
};

} // namespace hinkwise::presentation
