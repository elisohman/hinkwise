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
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cstddef>
#include <optional>

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
///
/// The same midpoint is what the non-investable assets are projected at: a flat
/// and a pension promise are not spendable capital, but they are not static
/// either, and Preserve is the band the model keeps value in.
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

/// The loan names as one phrase: "Flat", "Flat and Car", "Flat, Car and Boat".
QString joinNames(const QStringList& names)
{
    if (names.size() <= 1)
        return names.value(0);

    QStringList head = names;
    const QString last = head.takeLast();
    return PlanViewModel::tr("%1 and %2").arg(head.join(PlanViewModel::tr(", ")), last);
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
    m_debtWithoutTerms = snapshot.hasDebtWithoutTerms;
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
    // starting balances are investable capital: housing and pension promises
    // are left out, the same exclusion the cautious headline uses, so a
    // household that owns a flat does not look funded in year zero. Those
    // holdings are not dropped, though: they start otherAssets, so net worth
    // still counts the house a mortgage is secured on. The saving figure is
    // last month's, kept flat for the whole horizon: the page says so, because
    // pretending a household's saving rises with its income would be a forecast
    // dressed as arithmetic.
    m_baseInput.startingBalances = snapshot.facts.sheet.investablePerBucket;
    m_baseInput.startingOtherAssets =
        snapshot.facts.sheet.totalAssets.currency() == m_currency
            ? snapshot.facts.sheet.totalAssets - snapshot.facts.sheet.headlineAssets
            : domain::Money::zero(m_currency);
    m_baseInput.otherAssetsRealReturn = returnUsedFor(domain::Bucket::Preserve);
    m_baseInput.annualExpenses = snapshot.metrics.annualExpenses;
    m_baseInput.fiTarget = snapshot.metrics.fiTargetDefault;
    m_baseInput.years = kHorizonYears;

    // Debt is simulated loan by loan, each with its own rate, style and
    // monthly payment, so the split between a mortgage and a student loan
    // follows from their terms instead of from an aggregate amortisation. A
    // loan whose payment is zero keeps its balance flat: that is what "no terms
    // recorded yet" looks like to the model, and the page says so rather than
    // drawing a paydown nobody entered. A loan that also records a term is the
    // exception: its payment is recalculated from the term every year.
    m_baseInput.loans.clear();
    m_baseInput.loans.reserve(snapshot.debts.size());
    for (const application::DebtInEffect& debt : snapshot.debts) {
        m_baseInput.loans.push_back(domain::LoanTerms{debt.balance, debt.interestRate, debt.style,
                                                      debt.regularPayment, debt.monthlyFee,
                                                      debt.termMonths});
    }

    // What the terms themselves say, separately from what the projection draws
    // with them: a loan whose balance is positive and whose payment is missing
    // is one the page has to name, and a term whose implied payment differs from
    // the recorded one is a disagreement the household can settle.
    QStringList missingTerms;
    domain::Money recordedLoanPayment = domain::Money::zero(m_currency);
    for (const application::DebtInEffect& debt : snapshot.debts) {
        const bool paymentFollowsFromTerm =
            debt.style == domain::RepaymentStyle::Annuity && debt.termMonths > 0;
        if (debt.balance.isPositive() && !debt.regularPayment.isPositive() &&
            !paymentFollowsFromTerm)
            missingTerms.append(QString::fromStdString(debt.name));
        if (paymentFollowsFromTerm && debt.balance.isPositive())
            recordedLoanPayment += debt.regularPayment;
    }
    m_paymentMismatchNote.clear();
    if (snapshot.paymentDisagrees) {
        m_paymentMismatchNote =
            tr("CSN would charge about %1 a month; you recorded %2.")
                .arg(formatMoney(snapshot.computedLoanPayment), formatMoney(recordedLoanPayment));
    }
    m_missingTermsNote.clear();
    if (!missingTerms.isEmpty()) {
        const QString names = joinNames(missingTerms);
        m_missingTermsNote =
            missingTerms.size() == 1
                ? tr("%1 has no repayment terms recorded, so the debt is shown as unchanged. "
                     "Add them in Accounts & buckets.")
                      .arg(names)
                : tr("%1 have no repayment terms recorded, so the debt is shown as unchanged. "
                     "Add them in Accounts & buckets.")
                      .arg(names);
    }
    const std::optional<domain::SavingsBreakdown>& split = snapshot.facts.flow.savingSplit;
    // A breakdown in another currency is unusable: all three parts are added to
    // amounts in the profile's currency, so none of them may be mixed in.
    const bool splitKnown = split.has_value() && split->toBuffer.currency() == m_currency &&
                            split->toInvesting.currency() == m_currency &&
                            split->toAmortisation.currency() == m_currency;
    for (const domain::Bucket bucket : domain::allBuckets()) {
        const std::size_t index = static_cast<std::size_t>(bucket);
        m_baseInput.expectedRealReturn[index] = returnUsedFor(bucket);
        // Every slot is stated in the profile's currency, including the ones
        // that stay at zero: the domain adds them and refuses mixed units.
        m_baseInput.annualContributions[index] = domain::Money::zero(m_currency);
    }

    // What reaches the buckets. A recorded split says how much of the month's
    // saving stayed investable and how much went to the loan; only the first
    // part is capital, because the second turns into housing equity and the
    // projection already lets it shrink the debt. Using the whole saving here
    // as well would grow the buckets and the equity on the same krona.
    const domain::Money saving = snapshot.facts.flow.saving.currency() == m_currency
                                     ? snapshot.facts.flow.saving
                                     : domain::Money::zero(m_currency);
    m_monthlyInvestable = splitKnown ? split->toBuffer + split->toInvesting : saving;
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
    // A loan whose terms are missing always has a name, but if the snapshot
    // somehow carries the flag without the loan, the generic sentence still
    // says what is wrong rather than leaving the row blank.
    if (m_missingTermsNote.isEmpty() && snapshot.hasDebtWithoutTerms)
        m_missingTermsNote = m_amortisationNote;
    emit changed();
}

domain::ProjectionInput PlanViewModel::inputWith(domain::Money extraMonthly) const
{
    domain::ProjectionInput input = m_baseInput;
    if (m_contributionBucket < 0)
        return input;

    const domain::Money monthly = m_monthlyInvestable + extraMonthly;
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

    m_debtSeries.clear();
    for (const ProjectionPoint& point : shown.debt) {
        m_debtSeries.append(QVariantMap{{QStringLiteral("x"), point.yearRatio},
                                        {QStringLiteral("y"), point.ratio}});
    }
    m_netWorthSeries.clear();
    for (const ProjectionPoint& point : shown.netWorth) {
        m_netWorthSeries.append(QVariantMap{{QStringLiteral("x"), point.yearRatio},
                                            {QStringLiteral("y"), point.ratio}});
    }
    m_zeroLineRatio = shown.zeroRatio;

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

    m_hasDebt = shown.hasDebt;
    m_debtWithoutAmortisation = m_hasDebt && m_debtWithoutTerms;
    m_debtTodayLabel = shown.debt.isEmpty() ? QString{} : formatMoney(shown.debt.front().value);
    m_totalInterestLabel = formatMoney(shown.interestPaid);
    m_totalFeesLabel = shown.feesPaid.isPositive() ? formatMoney(shown.feesPaid) : QString{};
    m_costSummary =
        m_totalFeesLabel.isEmpty()
            ? tr("Interest over %1 years: %2").arg(kHorizonYears).arg(m_totalInterestLabel)
            : tr("Interest over %1 years: %2 \u00b7 Fees: %3")
                  .arg(kHorizonYears)
                  .arg(m_totalInterestLabel, m_totalFeesLabel);
    m_debtFreeLabel.clear();
    if (m_hasDebt && !m_debtWithoutAmortisation) {
        if (baseSeries.yearsToDebtFree < 0)
            m_debtFreeLabel = tr("Not within %1 years").arg(kHorizonYears);
        else if (baseSeries.yearsToDebtFree == 0)
            m_debtFreeLabel = tr("Today");
        else
            m_debtFreeLabel = QString::number(currentYear + baseSeries.yearsToDebtFree);
    }
    m_netWorthEndLabel =
        shown.netWorth.isEmpty() ? QString{} : formatMoney(shown.netWorth.back().value);
    m_amortisationNote =
        m_debtWithoutAmortisation
            ? tr("No monthly payment is recorded for a loan that still owes money, so its balance "
                 "does not fall in this projection. Record the payment on the loan.")
            : QString{};

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

    const domain::Money monthly = m_monthlyInvestable + m_extraMonthly;
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
