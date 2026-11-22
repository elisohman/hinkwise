// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plan_view_model.h"

#include "app_controller.h"
#include "catalog.h"
#include "domain/bucket.h"
#include "domain/percentage.h"
#include "fi_track.h"
#include "input_text.h"
#include "projection_series.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <cstddef>

namespace hinkwise::presentation {

namespace {

QString percentLabel(domain::Percentage percentage, int decimals)
{
    const QLocale locale;
    return PlanViewModel::tr("%1 %").arg(
        locale.toString(static_cast<double>(percentage.basisPoints()) / 100.0, 'f', decimals));
}

QString percentRangeLabel(const domain::PercentageRange& range)
{
    return PlanViewModel::tr("%1 – %2").arg(percentLabel(range.low, 1),
                                            percentLabel(range.high, 1));
}

/// The model's long-run real return for a bucket, taken at the middle of the
/// band it states. Play states no expectation at all, which is not the same as
/// zero, so it is reported as such rather than as a percentage the model never
/// claimed.
domain::Percentage returnUsedFor(domain::Bucket bucket)
{
    return domain::interpolate(domain::factsOf(bucket).realReturn,
                               domain::Percentage::fromBasisPoints(5000));
}

bool returnIsStated(domain::Bucket bucket)
{
    const domain::PercentageRange& range = domain::factsOf(bucket).realReturn;
    return !(range.low.isZero() && range.high.isZero());
}

} // namespace

PlanViewModel::PlanViewModel(QObject* parent)
    : QObject(parent)
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &PlanViewModel::reload);
    }
    reload();
}

QString PlanViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_currency));
}

void PlanViewModel::reload()
{
    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        apply(application::DashboardSnapshot{});
        return;
    }
    apply(controller->dashboardSnapshot());
}

void PlanViewModel::apply(const application::DashboardSnapshot& snapshot)
{
    const QLocale locale;
    m_hasProfile = snapshot.hasProfile;
    m_currency = snapshot.profile.baseCurrency;
    // The what-if amount belongs to the profile's currency; an amount typed
    // before the profile changed is dropped rather than added to another unit.
    if (m_extraMonthly.currency() != m_currency)
        m_extraMonthly = domain::Money::zero(m_currency);
    m_yearsOfFreedomLabel = locale.toString(snapshot.metrics.yearsOfFreedom.asFraction(), 'f', 1);

    const FiTrack track = fiTrackOf(snapshot.metrics, snapshot.profile.jurisdiction);
    m_fiTargetKnown = track.known;
    m_fiProgressLabel = percentLabel(snapshot.metrics.fiProgress, 1);
    m_fiProgressRatio = track.progressRatio;
    m_fiTargetLowRatio = track.lowRatio;
    m_fiTargetDefaultRatio = track.defaultRatio;
    m_fiTargetHighRatio = track.highRatio;
    m_fiMultiplierLow = track.lowMultiplier;
    m_fiMultiplierDefault = track.defaultMultiplier;
    m_fiMultiplierHigh = track.highMultiplier;
    m_fiTargetLowLabel = formatMoney(snapshot.metrics.fiTargetLow);
    m_fiTargetDefaultLabel = formatMoney(snapshot.metrics.fiTargetDefault);
    m_fiTargetHighLabel = formatMoney(snapshot.metrics.fiTargetHigh);

    // What the projection starts from, and what is added to it each year. The
    // saving figure is last month's, kept flat for the whole horizon: the page
    // says so, because pretending a household's saving rises with its income
    // would be a forecast dressed as arithmetic.
    m_baseInput.startingBalances = snapshot.facts.sheet.perBucket;
    m_baseInput.annualExpenses = snapshot.metrics.annualExpenses;
    m_baseInput.fiTarget = snapshot.metrics.fiTargetDefault;
    m_baseInput.years = kHorizonYears;
    for (const domain::Bucket bucket : domain::allBuckets()) {
        const std::size_t index = static_cast<std::size_t>(bucket);
        m_baseInput.expectedRealReturn[index] = returnUsedFor(bucket);
        // Every slot is stated in the profile's currency, including the ones
        // that stay at zero: the domain adds them and refuses mixed units.
        m_baseInput.annualContributions[index] = domain::Money::zero(m_currency);
    }

    m_monthlySaving = snapshot.facts.flow.saving.currency() == m_currency
                          ? snapshot.facts.flow.saving
                          : domain::Money::zero(m_currency);
    m_annualSaving = m_monthlySaving.scaledBy(domain::Percentage::fromBasisPoints(kMonthsPerYear));
    m_contributionBucket = snapshot.rebalance.nextContribution.has_value()
                               ? static_cast<int>(*snapshot.rebalance.nextContribution)
                               : -1;

    QVariantList assumptions;
    m_bucketLegend.clear();
    for (const domain::Bucket bucket : domain::allBuckets()) {
        const bool stated = returnIsStated(bucket);
        const QString used =
            stated ? percentLabel(returnUsedFor(bucket), 1) : QString::fromUtf8("\u2014");
        assumptions.append(
            QVariantMap{{QStringLiteral("label"), catalog::bucketLabel(bucket)},
                        {QStringLiteral("colourKey"), catalog::bucketColourKey(bucket)},
                        {QStringLiteral("usedLabel"), used},
                        {QStringLiteral("rangeLabel"),
                         stated ? percentRangeLabel(domain::factsOf(bucket).realReturn)
                                : QString::fromUtf8("\u2014")}});
        m_bucketLegend.append(
            QVariantMap{{QStringLiteral("label"), catalog::bucketLabel(bucket)},
                        {QStringLiteral("colourKey"), catalog::bucketColourKey(bucket)}});
    }
    m_assumptions = assumptions;

    recompute();
    emit changed();
}

domain::ProjectionInput PlanViewModel::inputWith(domain::Money extraMonthly) const
{
    domain::ProjectionInput input = m_baseInput;
    if (m_contributionBucket < 0)
        return input;

    const domain::Money monthly = m_monthlySaving + extraMonthly;
    input.annualContributions[static_cast<std::size_t>(m_contributionBucket)] =
        monthly.scaledBy(domain::Percentage::fromBasisPoints(kMonthsPerYear));
    return input;
}

void PlanViewModel::recompute()
{
    const domain::ProjectionInput base = inputWith(domain::Money::zero(m_currency));
    const domain::ProjectionInput withExtra = inputWith(m_extraMonthly);

    const ProjectionSeries baseSeries = projectionSeriesOf(base);
    const ProjectionSeries askedSeries = projectionSeriesOf(withExtra);
    const ProjectionSeries& shown = extraSavingActive() ? askedSeries : baseSeries;

    m_chartEmpty = !shown.valid;
    m_projectionBands.clear();
    for (const ProjectionBand& band : shown.bands) {
        m_projectionBands.append(QVariantMap{{QStringLiteral("x"), band.yearRatio},
                                             {QStringLiteral("bufferTop"), band.bufferTop},
                                             {QStringLiteral("preserveTop"), band.preserveTop},
                                             {QStringLiteral("growthTop"), band.growthTop},
                                             {QStringLiteral("playTop"), band.playTop}});
    }

    m_projectionYLabels.clear();
    for (const ProjectionTick& tick : shown.yTicks) {
        m_projectionYLabels.append(
            QVariantMap{{QStringLiteral("y"), tick.ratio},
                        {QStringLiteral("label"), formatMoneyPlain(tick.value)}});
    }

    // A calendar year per gridline is easier to place than "year 17": the axis
    // is labelled with the years a person will actually live through.
    const int currentYear = QDate::currentDate().year();
    m_projectionXLabels.clear();
    for (int offset = 0; offset <= kHorizonYears; offset += 5) {
        m_projectionXLabels.append(
            QVariantMap{{QStringLiteral("x"), static_cast<double>(offset) / kHorizonYears},
                        {QStringLiteral("label"), QString::number(currentYear + offset)}});
    }

    m_hasTargetLine = shown.valid && shown.hasTarget;
    m_targetLineRatio = shown.targetRatio;
    m_hasFiMarker = shown.valid && shown.yearsToTarget >= 0;
    m_fiMarkerRatio = shown.targetYearRatio;
    m_fiMarkerLabel =
        m_hasFiMarker ? tr("FI in %1").arg(currentYear + shown.yearsToTarget) : QString{};

    const auto yearsLabel = [this](int years) -> QString {
        if (years < 0)
            return tr("Not within %1 years").arg(kHorizonYears);
        if (years == 0)
            return tr("Already funded");
        return tr("%1 years from today").arg(years);
    };
    const auto yearLabel = [currentYear](int years) -> QString {
        return years < 0 ? QString{} : QString::number(currentYear + years);
    };

    m_baseYearsLabel = yearsLabel(baseSeries.yearsToTarget);
    m_baseYearLabel = yearLabel(baseSeries.yearsToTarget);
    m_extraYearsLabel = extraSavingActive() ? yearsLabel(askedSeries.yearsToTarget) : QString{};
    m_extraYearLabel = extraSavingActive() ? yearLabel(askedSeries.yearsToTarget) : QString{};

    m_fiDeltaLabel.clear();
    if (extraSavingActive() && m_fiTargetKnown) {
        const int baseYears = baseSeries.yearsToTarget;
        const int askedYears = askedSeries.yearsToTarget;
        if (askedYears < 0) {
            m_fiDeltaLabel =
                tr("The target is still out of reach within %1 years.").arg(kHorizonYears);
        } else if (baseYears < 0) {
            m_fiDeltaLabel = tr("The target becomes reachable; without the extra it does not.");
        } else if (baseYears > askedYears) {
            m_fiDeltaLabel =
                tr("%1 years earlier than at today's saving.").arg(baseYears - askedYears);
        } else {
            m_fiDeltaLabel = tr("The same year as at today's saving.");
        }
    }

    const domain::Money monthly = m_monthlySaving + m_extraMonthly;
    if (m_contributionBucket < 0) {
        m_chartCaption =
            tr("No new saving is added: in this phase the buckets are drawn down, not filled.");
    } else if (!monthly.isPositive()) {
        m_chartCaption =
            tr("Nothing is added each year yet: the curve grows what is already there.");
    } else {
        m_chartCaption =
            tr("%1 a month goes to %2, kept flat in today's money and added once a year.")
                .arg(formatMoney(monthly),
                     catalog::bucketLabel(static_cast<domain::Bucket>(m_contributionBucket)));
    }
}

void PlanViewModel::setExtraMonthlySaving(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        m_extraSavingError.clear();
        m_extraMonthly = domain::Money::zero(m_currency);
    } else {
        const MoneyInput parsed = parseMoneyInput(text, m_currency);
        if (!parsed.ok) {
            // The last amount that could be read stays on the chart: the field
            // says what is wrong, and the curve does not jump to zero mid-word.
            m_extraSavingError = parsed.error;
            emit changed();
            return;
        }
        m_extraSavingError.clear();
        m_extraMonthly = parsed.value;
    }
    recompute();
    emit changed();
}

} // namespace hinkwise::presentation
