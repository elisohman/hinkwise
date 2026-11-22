// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dashboard_view_model.h"

#include "app_controller.h"
#include "domain/insight.h"
#include "domain/phase.h"

#include <QLocale>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
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

/// How full the bar is. A bucket's target point is what it is aiming for; a
/// bucket with no target but a cap fills against the cap. Everything else has
/// nothing to be full of and stays at zero.
double fillRatioOf(domain::Money balance, const domain::ResolvedTarget& target)
{
    std::optional<domain::Money> denominator;
    if (target.hasTarget() && target.point.isPositive())
        denominator = target.point;
    else if (target.hasCap() && target.cap->isPositive())
        denominator = *target.cap;

    if (!denominator.has_value())
        return 0.0;

    const double ratio =
        static_cast<double>(balance.minorUnits()) / static_cast<double>(denominator->minorUnits());
    return std::clamp(ratio, 0.0, 1.0);
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
    case FillRatioRole:
        return QVariant{row.fillRatio};
    case ColourKeyRole:
        return QVariant{row.colourKey};
    case HasTargetRole:
        return QVariant{row.hasTarget};
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
        {FillRatioRole, "fillRatio"},
        {ColourKeyRole, "colourKey"},
        {HasTargetRole, "hasTarget"},
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
        {SeverityRole, "severity"},
        {TitleRole, "title"},
        {WhyRole, "why"},
        {SuggestionRole, "suggestion"},
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
    m_hasProfile = snapshot.hasProfile;
    m_phaseLabel = QString::fromUtf8(domain::labelOf(snapshot.profile.phase));
    m_currencyLabel = QString::fromLatin1(domain::isoCodeOf(snapshot.profile.baseCurrency));
    m_yearsOfFreedom = snapshot.metrics.yearsOfFreedom.basisPoints() / 10000.0;

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

        bucketRows.push_back(BucketRow{
            QString::fromUtf8(domain::labelOf(bucket)),
            horizonLabel(bucket),
            formatMoney(balance),
            targetLabel(target),
            fillRatioOf(balance, target),
            colourKeyOf(bucket),
            target.hasTarget(),
        });
    }
    m_buckets->setRows(std::move(bucketRows));

    QVector<InsightRow> insightRows;
    insightRows.reserve(static_cast<qsizetype>(snapshot.insights.size()));
    for (const domain::Insight& insight : snapshot.insights) {
        insightRows.push_back(InsightRow{
            QString::fromUtf8(domain::labelOf(insight.severity)),
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
