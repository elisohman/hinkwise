// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"
#include "domain/bucket.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/targets.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace hinkwise::presentation {

/// One bucket as a screen reads it: the labels are already formatted, and the
/// fill ratio is a plain 0..1 number for the progress bar.
struct BucketRow {
    QString label;
    QString horizonLabel;
    QString balanceLabel;
    QString targetLabel;
    double fillRatio = 0.0;
    QString colourKey;
    bool hasTarget = false;
};

/// The bucket rows as a QML list model, in fill order.
class BucketsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        HorizonLabelRole,
        BalanceLabelRole,
        TargetLabelRole,
        FillRatioRole,
        ColourKeyRole,
        HasTargetRole,
    };

    explicit BucketsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<BucketRow> rows);

private:
    QVector<BucketRow> m_rows;
};

/// One insight as a screen reads it. The text is the rule's own coaching, not
/// a rephrasing here, so the message and the check cannot drift apart.
struct InsightRow {
    QString severity;
    QString title;
    QString why;
    QString suggestion;
};

/// The rule results as a QML list model, most urgent first.
class InsightsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role {
        SeverityRole = Qt::UserRole + 1,
        TitleRole,
        WhyRole,
        SuggestionRole,
    };

    explicit InsightsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<InsightRow> rows);

private:
    QVector<InsightRow> m_rows;
};

/// The dashboard as QML sees it.
///
/// The view model never does arithmetic the model cares about: it asks the
/// application layer for a DashboardSnapshot and turns each figure into text
/// with QLocale. Money keeps its ISO code, because "kr" and "$" are ambiguous
/// on a machine with more than one household currency.
class DashboardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
    Q_PROPERTY(QString currencyLabel READ currencyLabel NOTIFY changed)
    Q_PROPERTY(double yearsOfFreedom READ yearsOfFreedom NOTIFY changed)
    Q_PROPERTY(QString annualExpensesLabel READ annualExpensesLabel NOTIFY changed)
    Q_PROPERTY(QString monthlyExpensesLabel READ monthlyExpensesLabel NOTIFY changed)
    Q_PROPERTY(QString bufferMonthsLabel READ bufferMonthsLabel NOTIFY changed)
    Q_PROPERTY(QString netWorthLabel READ netWorthLabel NOTIFY changed)
    Q_PROPERTY(QString financialAssetsLabel READ financialAssetsLabel NOTIFY changed)
    Q_PROPERTY(QString headlineNetWorthLabel READ headlineNetWorthLabel NOTIFY changed)
    Q_PROPERTY(QString pensionAndHousingShareLabel READ pensionAndHousingShareLabel NOTIFY changed)
    Q_PROPERTY(QString crashImpactLabel READ crashImpactLabel NOTIFY changed)
    Q_PROPERTY(QString savingsRateLabel READ savingsRateLabel NOTIFY changed)
    Q_PROPERTY(QString fiProgressLabel READ fiProgressLabel NOTIFY changed)
    Q_PROPERTY(int insightsCount READ insightsCount NOTIFY changed)
    Q_PROPERTY(hinkwise::presentation::BucketsModel* buckets READ buckets CONSTANT)
    Q_PROPERTY(hinkwise::presentation::InsightsModel* insights READ insights CONSTANT)

public:
    explicit DashboardViewModel(QObject* parent = nullptr);

    /// Rebuilds every value from the controller's current data.
    Q_INVOKABLE void reload();

    bool hasProfile() const { return m_hasProfile; }
    QString phaseLabel() const { return m_phaseLabel; }
    QString currencyLabel() const { return m_currencyLabel; }
    double yearsOfFreedom() const { return m_yearsOfFreedom; }
    QString annualExpensesLabel() const { return m_annualExpensesLabel; }
    QString monthlyExpensesLabel() const { return m_monthlyExpensesLabel; }
    QString bufferMonthsLabel() const { return m_bufferMonthsLabel; }
    QString netWorthLabel() const { return m_netWorthLabel; }
    QString financialAssetsLabel() const { return m_financialAssetsLabel; }
    QString headlineNetWorthLabel() const { return m_headlineNetWorthLabel; }
    QString pensionAndHousingShareLabel() const { return m_pensionAndHousingShareLabel; }
    QString crashImpactLabel() const { return m_crashImpactLabel; }
    QString savingsRateLabel() const { return m_savingsRateLabel; }
    QString fiProgressLabel() const { return m_fiProgressLabel; }
    int insightsCount() const { return m_insightsCount; }
    BucketsModel* buckets() const { return m_buckets; }
    InsightsModel* insights() const { return m_insights; }

signals:
    void changed();

private:
    void apply(const application::DashboardSnapshot& snapshot);

    QString horizonLabel(domain::Bucket bucket) const;
    QString targetLabel(const domain::ResolvedTarget& target) const;
    QString monthsLabel(domain::Percentage months) const;
    QString percentLabel(domain::Percentage percentage) const;

    bool m_hasProfile = false;
    QString m_phaseLabel;
    QString m_currencyLabel;
    double m_yearsOfFreedom = 0.0;
    QString m_annualExpensesLabel;
    QString m_monthlyExpensesLabel;
    QString m_bufferMonthsLabel;
    QString m_netWorthLabel;
    QString m_financialAssetsLabel;
    QString m_headlineNetWorthLabel;
    QString m_pensionAndHousingShareLabel;
    QString m_crashImpactLabel;
    QString m_savingsRateLabel;
    QString m_fiProgressLabel;
    int m_insightsCount = 0;
    BucketsModel* m_buckets = nullptr;
    InsightsModel* m_insights = nullptr;
};

} // namespace hinkwise::presentation
