// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dashboard_view_model.h"

#include "app_controller.h"
#include "domain/insight.h"
#include "domain/phase.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace hinkwise::presentation {

namespace {

/// Money becomes text here and nowhere else: the amount is exact until the
/// formatter sees it, and the ISO code travels with it so the household never
/// has to guess which currency a figure is in.
QString formatMoney(const domain::Money& amount)
{
    const QLocale locale;
    const double divisor =
        std::pow(10.0, static_cast<double>(domain::traitsOf(amount.currency()).minorDigits));
    return locale.toCurrencyString(static_cast<double>(amount.minorUnits()) / divisor,
                                   QString::fromLatin1(domain::isoCodeOf(amount.currency())));
}

QString colourKeyOf(domain::Bucket bucket)
{
    switch (bucket) {
    case domain::Bucket::Buffer:
        return QStringLiteral("buffer");
    case domain::Bucket::Preserve:
        return QStringLiteral("preserve");
    case domain::Bucket::Growth:
        return QStringLiteral("growth");
    case domain::Bucket::Play:
        return QStringLiteral("play");
    }
    return QStringLiteral("buffer");
}

/// The track a bucket's gauge draws on: the widest thing it has to show, plus
/// a margin, so neither the band nor a balance that overshoots it ends up on
/// the rim where it cannot be read.
double gaugeAxis(domain::Money balance, const domain::ResolvedTarget& target)
{
    double widest = std::max(static_cast<double>(balance.minorUnits()), 1.0);
    if (target.hasTarget())
        widest = std::max(widest, static_cast<double>(target.band.high.minorUnits()));
    if (target.hasCap())
        widest = std::max(widest, static_cast<double>(target.cap->minorUnits()));
    return widest * 1.08;
}

double trackPosition(domain::Money amount, double axis)
{
    if (axis <= 0.0)
        return 0.0;
    return std::clamp(static_cast<double>(amount.minorUnits()) / axis, 0.0, 1.0);
}

QString severityKeyOf(domain::Severity severity)
{
    switch (severity) {
    case domain::Severity::Critical:
        return QStringLiteral("critical");
    case domain::Severity::Warning:
        return QStringLiteral("warning");
    case domain::Severity::Advice:
        return QStringLiteral("advice");
    case domain::Severity::Info:
        return QStringLiteral("info");
    }
    return QStringLiteral("info");
}

} // namespace

// ---------------------------------------------------------------------------
// Buckets
// ---------------------------------------------------------------------------

BucketsModel::BucketsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BucketsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant BucketsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const BucketRow& row = m_rows[index.row()];
    switch (role) {
    case LabelRole:
        return QVariant{row.label};
    case HorizonLabelRole:
        return QVariant{row.horizonLabel};
    case BalanceLabelRole:
        return QVariant{row.balanceLabel};
    case TargetLabelRole:
        return QVariant{row.targetLabel};
    case ColourKeyRole:
        return QVariant{row.colourKey};
    case BandStartRole:
        return QVariant{row.bandStart};
    case BandEndRole:
        return QVariant{row.bandEnd};
    case BalanceMarkerRole:
        return QVariant{row.balanceMarker};
    case CapMarkerRole:
        return QVariant{row.capMarker};
    case HasTargetRole:
        return QVariant{row.hasTarget};
    case HasCapRole:
        return QVariant{row.hasCap};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> BucketsModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {HorizonLabelRole, "horizonLabel"},
        {BalanceLabelRole, "balanceLabel"},
        {TargetLabelRole, "targetLabel"},
        {ColourKeyRole, "colourKey"},
        {BandStartRole, "bandStart"},
        {BandEndRole, "bandEnd"},
        {BalanceMarkerRole, "balanceMarker"},
        {CapMarkerRole, "capMarker"},
        {HasTargetRole, "hasTarget"},
        {HasCapRole, "hasCap"},
    };
}

void BucketsModel::setRows(QVector<BucketRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

// ---------------------------------------------------------------------------
// Insights
// ---------------------------------------------------------------------------

InsightsModel::InsightsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int InsightsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant InsightsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const InsightRow& row = m_rows[index.row()];
    switch (role) {
    case SeverityRole:
        return QVariant{row.severity};
    case SeverityKeyRole:
        return QVariant{row.severityKey};
    case TitleRole:
        return QVariant{row.title};
    case WhyRole:
        return QVariant{row.why};
    case SuggestionRole:
        return QVariant{row.suggestion};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> InsightsModel::roleNames() const
{
    return {
        {SeverityRole, "severity"}, {SeverityKeyRole, "severityKey"}, {TitleRole, "title"},
        {WhyRole, "why"},           {SuggestionRole, "suggestion"},
    };
}

void InsightsModel::setRows(QVector<InsightRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------

DashboardViewModel::DashboardViewModel(QObject* parent)
    : QObject(parent)
    , m_buckets(new BucketsModel(this))
    , m_insights(new InsightsModel(this))
{
}

void DashboardViewModel::reload()
{
    const AppController* controller = AppController::instance();
    apply(controller != nullptr ? controller->dashboardSnapshot()
                                : application::DashboardSnapshot{});
}

void DashboardViewModel::apply(const application::DashboardSnapshot& snapshot)
{
    const QLocale locale;

    m_hasProfile = snapshot.hasProfile;
    // AppController builds the snapshot for the current month, so the header
    // names the same month the figures describe.
    m_monthLabel = locale.toString(QDate::currentDate(), QStringLiteral("MMMM yyyy"));
    m_phaseLabel = QString::fromUtf8(domain::labelOf(snapshot.profile.phase));
    m_currencyLabel = QString::fromLatin1(domain::isoCodeOf(snapshot.profile.baseCurrency));
    m_yearsOfFreedom = snapshot.metrics.yearsOfFreedom.basisPoints() / 10000.0;
    m_yearsOfFreedomLabel = locale.toString(m_yearsOfFreedom, 'f', 1);

    // FI progress and the x21/x25/x33 band share one track. Every position is
    // a ratio of the default target: the ticks come from the jurisdiction's
    // multipliers, the progress from the metric the model already computed,
    // so no money figure is divided anywhere outside the domain.
    const domain::Jurisdiction& jurisdiction = snapshot.profile.jurisdiction;
    m_fiMultiplierLow = jurisdiction.fiMultiplierLow;
    m_fiMultiplierDefault = jurisdiction.fiMultiplierDefault;
    m_fiMultiplierHigh = jurisdiction.fiMultiplierHigh;
    m_fiTargetKnown = snapshot.metrics.fiTargetDefault.isPositive();

    m_fiProgressRatio = 0.0;
    m_fiTargetLowRatio = 0.0;
    m_fiTargetDefaultRatio = 0.0;
    m_fiTargetHighRatio = 0.0;
    if (m_fiTargetKnown && m_fiMultiplierDefault > 0) {
        const double defaultMultiplier = static_cast<double>(m_fiMultiplierDefault);
        const double progress = snapshot.metrics.fiProgress.asFraction();
        const double high = static_cast<double>(m_fiMultiplierHigh) / defaultMultiplier;
        const double axis = std::max({1.0, high, progress}) * 1.08;
        m_fiProgressRatio = std::clamp(progress / axis, 0.0, 1.0);
        m_fiTargetLowRatio =
            std::clamp(static_cast<double>(m_fiMultiplierLow) / defaultMultiplier / axis, 0.0, 1.0);
        m_fiTargetDefaultRatio = std::clamp(1.0 / axis, 0.0, 1.0);
        m_fiTargetHighRatio = std::clamp(high / axis, 0.0, 1.0);
    }

    m_annualExpensesLabel = formatMoney(snapshot.metrics.annualExpenses);
    m_monthlyExpensesLabel = formatMoney(snapshot.metrics.monthlyExpenses);
    m_bufferMonthsLabel = monthsLabel(snapshot.metrics.bufferMonths);
    m_netWorthLabel = formatMoney(snapshot.facts.sheet.netWorth);
    m_financialAssetsLabel = formatMoney(snapshot.metrics.financialAssets);
    m_headlineNetWorthLabel = formatMoney(snapshot.facts.sheet.headlineNetWorth(false));
    m_pensionAndHousingShareLabel = percentLabel(snapshot.metrics.pensionAndHousingShare);
    m_crashImpactLabel = percentLabel(snapshot.metrics.crashImpact);
    m_savingsRateLabel = percentLabel(snapshot.metrics.savingsRate);
    m_fiProgressLabel = percentLabel(snapshot.metrics.fiProgress);

    QVector<BucketRow> bucketRows;
    bucketRows.reserve(static_cast<qsizetype>(domain::allBuckets().size()));
    for (const domain::Bucket bucket : domain::allBuckets()) {
        const std::size_t index = static_cast<std::size_t>(bucket);
        const domain::Money balance = snapshot.facts.sheet.perBucket[index];
        const domain::ResolvedTarget& target = snapshot.facts.targets[index];
        const double axis = gaugeAxis(balance, target);

        bucketRows.push_back(BucketRow{
            QString::fromUtf8(domain::labelOf(bucket)),
            horizonLabel(bucket),
            formatMoney(balance),
            targetLabel(target),
            colourKeyOf(bucket),
            target.hasTarget() ? trackPosition(target.band.low, axis) : 0.0,
            target.hasTarget() ? trackPosition(target.band.high, axis) : 0.0,
            trackPosition(balance, axis),
            target.hasCap() ? trackPosition(*target.cap, axis) : 0.0,
            target.hasTarget(),
            target.hasCap(),
        });
    }
    m_buckets->setRows(std::move(bucketRows));

    QVector<InsightRow> insightRows;
    insightRows.reserve(static_cast<qsizetype>(snapshot.insights.size()));
    for (const domain::Insight& insight : snapshot.insights) {
        insightRows.push_back(InsightRow{
            QString::fromUtf8(domain::labelOf(insight.severity)),
            severityKeyOf(insight.severity),
            QString::fromStdString(insight.title),
            QString::fromStdString(insight.why),
            QString::fromStdString(insight.suggestion),
        });
    }
    m_insightsCount = static_cast<int>(insightRows.size());
    m_insights->setRows(std::move(insightRows));

    emit changed();
}

QString DashboardViewModel::horizonLabel(domain::Bucket bucket) const
{
    const domain::Horizon& horizon = domain::factsOf(bucket).horizon;
    if (!horizon.hasUpperBound()) {
        if (horizon.minYears > 0)
            return tr("%1+ years").arg(horizon.minYears);
        return tr("any horizon");
    }
    if (horizon.minYears == 0)
        return tr("under %1 years").arg(*horizon.maxYears);
    return tr("%1–%2 years").arg(horizon.minYears).arg(*horizon.maxYears);
}

QString DashboardViewModel::targetLabel(const domain::ResolvedTarget& target) const
{
    if (target.hasTarget()) {
        return tr("Target: %1 – %2")
            .arg(formatMoney(target.band.low), formatMoney(target.band.high));
    }
    if (target.hasCap())
        return tr("Cap: %1").arg(formatMoney(*target.cap));
    if (target.kind == domain::BucketTarget::Kind::AsLargeAsPossible)
        return tr("As large as possible");
    return tr("No target");
}

QString DashboardViewModel::monthsLabel(domain::Percentage months) const
{
    const QLocale locale;
    return tr("%1 months").arg(locale.toString(months.basisPoints() / 10000.0, 'f', 1));
}

QString DashboardViewModel::percentLabel(domain::Percentage percentage) const
{
    const QLocale locale;
    return tr("%1 %").arg(locale.toString(percentage.basisPoints() / 100.0, 'f', 1));
}

} // namespace hinkwise::presentation
