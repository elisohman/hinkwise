// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/dashboard.h"
#include "domain/money.h"
#include "domain/projection.h"
#include "domain/year_month.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace hinkwise::presentation {

/// The plan page: how many years of freedom the household has now, where that
/// sits against the x21 / x25 / x33 band, and what the future looks like if
/// today's saving continues.
///
/// Every series comes from domain::project(); this class only chooses the
/// inputs, hands the results to projectionSeriesOf() and formats the labels.
/// The "what if" amount changes one input and asks the domain again, so the
/// curve with an extra saving is drawn by the same code as the curve without it.
class PlanViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// How far ahead the projection looks. A straight line over three decades
    /// is an assumption, not a forecast, and the page says so.
    Q_PROPERTY(int horizonYears READ horizonYears CONSTANT)

    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(QString currencyCode READ currencyCode NOTIFY changed)
    Q_PROPERTY(QString yearsOfFreedomLabel READ yearsOfFreedomLabel NOTIFY changed)
    Q_PROPERTY(bool fiTargetKnown READ fiTargetKnown NOTIFY changed)
    Q_PROPERTY(QString fiProgressLabel READ fiProgressLabel NOTIFY changed)
    Q_PROPERTY(double fiProgressRatio READ fiProgressRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetLowRatio READ fiTargetLowRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetDefaultRatio READ fiTargetDefaultRatio NOTIFY changed)
    Q_PROPERTY(double fiTargetHighRatio READ fiTargetHighRatio NOTIFY changed)
    Q_PROPERTY(int fiMultiplierLow READ fiMultiplierLow NOTIFY changed)
    Q_PROPERTY(int fiMultiplierDefault READ fiMultiplierDefault NOTIFY changed)
    Q_PROPERTY(int fiMultiplierHigh READ fiMultiplierHigh NOTIFY changed)
    Q_PROPERTY(QString fiTargetLowLabel READ fiTargetLowLabel NOTIFY changed)
    Q_PROPERTY(QString fiTargetDefaultLabel READ fiTargetDefaultLabel NOTIFY changed)
    Q_PROPERTY(QString fiTargetHighLabel READ fiTargetHighLabel NOTIFY changed)

    Q_PROPERTY(QVariantList projectionBands READ projectionBands NOTIFY changed)
    Q_PROPERTY(QVariantList projectionYLabels READ projectionYLabels NOTIFY changed)
    Q_PROPERTY(QVariantList projectionXLabels READ projectionXLabels NOTIFY changed)
    Q_PROPERTY(QVariantList debtSeries READ debtSeries NOTIFY changed)
    Q_PROPERTY(QVariantList netWorthSeries READ netWorthSeries NOTIFY changed)
    Q_PROPERTY(double zeroLineRatio READ zeroLineRatio NOTIFY changed)
    Q_PROPERTY(QVariantList bucketLegend READ bucketLegend NOTIFY changed)
    Q_PROPERTY(bool chartEmpty READ chartEmpty NOTIFY changed)

    /// The household's debt, and whether the projection can make it fall.
    Q_PROPERTY(bool hasDebt READ hasDebt NOTIFY changed)
    Q_PROPERTY(bool debtWithoutAmortisation READ debtWithoutAmortisation NOTIFY changed)
    Q_PROPERTY(QString debtTodayLabel READ debtTodayLabel NOTIFY changed)
    Q_PROPERTY(QString debtFreeLabel READ debtFreeLabel NOTIFY changed)
    Q_PROPERTY(QString netWorthEndLabel READ netWorthEndLabel NOTIFY changed)
    Q_PROPERTY(QString amortisationNote READ amortisationNote NOTIFY changed)
    Q_PROPERTY(bool hasTargetLine READ hasTargetLine NOTIFY changed)
    Q_PROPERTY(double targetLineRatio READ targetLineRatio NOTIFY changed)
    Q_PROPERTY(bool hasFiMarker READ hasFiMarker NOTIFY changed)
    Q_PROPERTY(double fiMarkerRatio READ fiMarkerRatio NOTIFY changed)
    Q_PROPERTY(QString fiMarkerLabel READ fiMarkerLabel NOTIFY changed)
    Q_PROPERTY(QString chartCaption READ chartCaption NOTIFY changed)

    Q_PROPERTY(QString extraSavingError READ extraSavingError NOTIFY changed)
    Q_PROPERTY(bool extraSavingActive READ extraSavingActive NOTIFY changed)
    Q_PROPERTY(bool extraSavingAvailable READ extraSavingAvailable NOTIFY changed)
    Q_PROPERTY(QString baseYearsLabel READ baseYearsLabel NOTIFY changed)
    Q_PROPERTY(QString baseYearLabel READ baseYearLabel NOTIFY changed)
    Q_PROPERTY(QString extraYearsLabel READ extraYearsLabel NOTIFY changed)
    Q_PROPERTY(QString extraYearLabel READ extraYearLabel NOTIFY changed)
    Q_PROPERTY(QString fiDeltaLabel READ fiDeltaLabel NOTIFY changed)

    Q_PROPERTY(QVariantList assumptions READ assumptions NOTIFY changed)

public:
    explicit PlanViewModel(QObject* parent = nullptr);

    /// Rebuilds every figure from the controller's current data, keeping the
    /// what-if amount the household typed.
    Q_INVOKABLE void reload();

    /// Reads the extra monthly saving as money and asks the domain for a fresh
    /// projection. An empty field means no extra; an unreadable one leaves the
    /// last valid amount in place and reports why under the field.
    Q_INVOKABLE void setExtraMonthlySaving(const QString& text);

    int horizonYears() const { return kHorizonYears; }
    bool hasProfile() const { return m_hasProfile; }
    QString currencyCode() const;
    QString yearsOfFreedomLabel() const { return m_yearsOfFreedomLabel; }
    bool fiTargetKnown() const { return m_fiTargetKnown; }
    QString fiProgressLabel() const { return m_fiProgressLabel; }
    double fiProgressRatio() const { return m_fiProgressRatio; }
    double fiTargetLowRatio() const { return m_fiTargetLowRatio; }
    double fiTargetDefaultRatio() const { return m_fiTargetDefaultRatio; }
    double fiTargetHighRatio() const { return m_fiTargetHighRatio; }
    int fiMultiplierLow() const { return m_fiMultiplierLow; }
    int fiMultiplierDefault() const { return m_fiMultiplierDefault; }
    int fiMultiplierHigh() const { return m_fiMultiplierHigh; }
    QString fiTargetLowLabel() const { return m_fiTargetLowLabel; }
    QString fiTargetDefaultLabel() const { return m_fiTargetDefaultLabel; }
    QString fiTargetHighLabel() const { return m_fiTargetHighLabel; }
    QVariantList projectionBands() const { return m_projectionBands; }
    QVariantList projectionYLabels() const { return m_projectionYLabels; }
    QVariantList projectionXLabels() const { return m_projectionXLabels; }
    QVariantList debtSeries() const { return m_debtSeries; }
    QVariantList netWorthSeries() const { return m_netWorthSeries; }
    double zeroLineRatio() const { return m_zeroLineRatio; }
    QVariantList bucketLegend() const { return m_bucketLegend; }
    bool chartEmpty() const { return m_chartEmpty; }
    bool hasDebt() const { return m_hasDebt; }
    bool debtWithoutAmortisation() const { return m_debtWithoutAmortisation; }
    QString debtTodayLabel() const { return m_debtTodayLabel; }
    QString debtFreeLabel() const { return m_debtFreeLabel; }
    QString netWorthEndLabel() const { return m_netWorthEndLabel; }
    QString amortisationNote() const { return m_amortisationNote; }
    bool hasTargetLine() const { return m_hasTargetLine; }
    double targetLineRatio() const { return m_targetLineRatio; }
    bool hasFiMarker() const { return m_hasFiMarker; }
    double fiMarkerRatio() const { return m_fiMarkerRatio; }
    QString fiMarkerLabel() const { return m_fiMarkerLabel; }
    QString chartCaption() const { return m_chartCaption; }
    QString extraSavingError() const { return m_extraSavingError; }
    bool extraSavingActive() const { return m_extraMonthly.isPositive(); }
    /// False in the preserving phases, where the model has no next
    /// contribution to add to: an extra saving has no bucket to go to.
    bool extraSavingAvailable() const { return m_contributionBucket >= 0; }
    QString baseYearsLabel() const { return m_baseYearsLabel; }
    QString baseYearLabel() const { return m_baseYearLabel; }
    QString extraYearsLabel() const { return m_extraYearsLabel; }
    QString extraYearLabel() const { return m_extraYearLabel; }
    QString fiDeltaLabel() const { return m_fiDeltaLabel; }
    QVariantList assumptions() const { return m_assumptions; }

signals:
    void changed();

private:
    static constexpr int kHorizonYears = 30;
    /// A monthly amount over a year, as the domain's own scaling: 120 000 bp.
    static constexpr int kMonthsPerYear = 120000;

    void apply(const application::DashboardSnapshot& snapshot);
    /// Rebuilds the chart and the FI labels from m_baseInput and the what-if
    /// amount. The only arithmetic here is choosing the contribution; the
    /// years themselves come from domain::project().
    void recompute();
    domain::ProjectionInput inputWith(domain::Money extraMonthly) const;

    bool m_hasProfile = false;
    domain::Currency m_currency = domain::Currency::Sek;
    domain::ProjectionInput m_baseInput;
    /// The monthly amount that actually reaches the buckets this month: the
    /// part the recorded split sends to the buffer and the investable buckets.
    /// With no split it is the whole recorded saving. Amortisation is never in
    /// it: that krona becomes housing equity, and the projection already lets it
    /// shrink the debt.
    domain::Money m_monthlyInvestable;
    int m_contributionBucket = -1;
    domain::Money m_extraMonthly;
    QString m_extraSavingError;

    QString m_yearsOfFreedomLabel;
    bool m_fiTargetKnown = false;
    QString m_fiProgressLabel;
    double m_fiProgressRatio = 0.0;
    double m_fiTargetLowRatio = 0.0;
    double m_fiTargetDefaultRatio = 0.0;
    double m_fiTargetHighRatio = 0.0;
    int m_fiMultiplierLow = 0;
    int m_fiMultiplierDefault = 0;
    int m_fiMultiplierHigh = 0;
    QString m_fiTargetLowLabel;
    QString m_fiTargetDefaultLabel;
    QString m_fiTargetHighLabel;

    QVariantList m_projectionBands;
    QVariantList m_projectionYLabels;
    QVariantList m_projectionXLabels;
    QVariantList m_debtSeries;
    QVariantList m_netWorthSeries;
    double m_zeroLineRatio = 0.0;
    QVariantList m_bucketLegend;
    bool m_chartEmpty = true;
    bool m_hasDebt = false;
    bool m_debtWithoutAmortisation = false;
    QString m_debtTodayLabel;
    QString m_debtFreeLabel;
    QString m_netWorthEndLabel;
    QString m_amortisationNote;
    bool m_hasTargetLine = false;
    double m_targetLineRatio = 0.0;
    bool m_hasFiMarker = false;
    double m_fiMarkerRatio = 0.0;
    QString m_fiMarkerLabel;
    QString m_chartCaption;

    QString m_baseYearsLabel;
    QString m_baseYearLabel;
    QString m_extraYearsLabel;
    QString m_extraYearLabel;
    QString m_fiDeltaLabel;

    QVariantList m_assumptions;
};

} // namespace hinkwise::presentation
