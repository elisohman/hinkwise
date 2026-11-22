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
/// band, the balance and the cap are plain 0..1 positions along one shared
/// track, so a gauge only multiplies them by pixels.
struct BucketRow {
    QString label;
    QString horizonLabel;
    QString balanceLabel;
    QString targetLabel;
    QString colourKey;
    double bandStart = 0.0;
    double bandEnd = 0.0;
    double balanceMarker = 0.0;
    double capMarker = 0.0;
    bool hasTarget = false;
    bool hasCap = false;
};

/// The four buckets as a screen reads them, resolved from one snapshot: the
/// labels already formatted, and the band, balance and cap already turned into
/// positions along one shared track. The dashboard and the buckets page read
/// the same household through this, so the two cannot draw it differently.
QVector<BucketRow> bucketRowsOf(const application::DashboardSnapshot& snapshot);

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
        ColourKeyRole,
        BandStartRole,
        BandEndRole,
        BalanceMarkerRole,
        CapMarkerRole,
        HasTargetRole,
        HasCapRole,
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
/// `severityKey` is the untranslated handle a screen colours the entry by, in
/// the same way colourKey names a bucket.
struct InsightRow {
    QString severity;
    QString severityKey;
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
        SeverityKeyRole,
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
/// with QLocale. It reloads whenever AppController reports a write, so the
/// dashboard cannot describe a household some other page has already changed. Money keeps its ISO
/// code, because "kr" and "$" are ambiguous on a machine with more than one household currency.
class DashboardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(QString monthLabel READ monthLabel NOTIFY changed)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
    Q_PROPERTY(QString currencyLabel READ currencyLabel NOTIFY changed)
    Q_PROPERTY(double yearsOfFreedom READ yearsOfFreedom NOTIFY changed)
    Q_PROPERTY(QString yearsOfFreedomLabel READ yearsOfFreedomLabel NOTIFY changed)
    Q_PROPERTY(bool fiTargetKnown READ fiTargetKnown NOTIFY changed)
    Q_PROPERTY(double fiProgressRatio READ fiProgressRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetLowRatio READ fiTargetLowRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetDefaultRatio READ fiTargetDefaultRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetHighRatio READ fiTargetHighRatio NOTIFY changed)
    Q_PROPERTY(int fiMultiplierLow READ fiMultiplierLow NOTIFY changed)
    Q_PROPERTY(int fiMultiplierDefault READ fiMultiplierDefault NOTIFY changed)
    Q_PROPERTY(int fiMultiplierHigh READ fiMultiplierHigh NOTIFY changed)
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
    QString monthLabel() const { return m_monthLabel; }
    QString phaseLabel() const { return m_phaseLabel; }
    QString currencyLabel() const { return m_currencyLabel; }
    double yearsOfFreedom() const { return m_yearsOfFreedom; }
    QString yearsOfFreedomLabel() const { return m_yearsOfFreedomLabel; }
    bool fiTargetKnown() const { return m_fiTargetKnown; }
    double fiProgressRatio() const { return m_fiProgressRatio; }
    double fiTargetLowRatio() const { return m_fiTargetLowRatio; }
    double fiTargetDefaultRatio() const { return m_fiTargetDefaultRatio; }
    double fiTargetHighRatio() const { return m_fiTargetHighRatio; }
    int fiMultiplierLow() const { return m_fiMultiplierLow; }
    int fiMultiplierDefault() const { return m_fiMultiplierDefault; }
    int fiMultiplierHigh() const { return m_fiMultiplierHigh; }
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

    QString monthsLabel(domain::Percentage months) const;
    QString percentLabel(domain::Percentage percentage) const;

    bool m_hasProfile = false;
    QString m_monthLabel;
    QString m_phaseLabel;
    QString m_currencyLabel;
    double m_yearsOfFreedom = 0.0;
    QString m_yearsOfFreedomLabel;
    bool m_fiTargetKnown = false;
    double m_fiProgressRatio = 0.0;
    double m_fiTargetLowRatio = 0.0;
    double m_fiTargetDefaultRatio = 0.0;
    double m_fiTargetHighRatio = 0.0;
    int m_fiMultiplierLow = 0;
    int m_fiMultiplierDefault = 0;
    int m_fiMultiplierHigh = 0;
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
