// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cashflow_series.h"

#include "domain/metrics.h"

#include <QCoreApplication>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace hinkwise::presentation {

namespace {

QString message(const char* source)
{
    return QCoreApplication::translate("Cashflow", source);
}

} // namespace

IncomeSplit incomeSplitOf(const domain::MonthFlow& flow)
{
    // The split is the domain's, not a second division here: the dashboard, the
    // rules and this page all read needs/wants/saving out of computeMetrics().
    domain::HouseholdFacts facts;
    facts.flow = flow;
    const domain::Metrics metrics = domain::computeMetrics(facts);

    IncomeSplit split;
    split.hasIncome = flow.income.isPositive();
    split.needs = metrics.needsShare;
    split.wants = metrics.wantsShare;
    split.saving = metrics.savingShare;
    return split;
}

SavingsSplitShares savingsSplitSharesOf(const domain::MonthFlow& flow)
{
    SavingsSplitShares shares;
    if (!flow.savingSplit)
        return shares;

    // The three shares come from the domain's metrics, exactly as the income
    // split does above. A fallback division here would be a second definition
    // of what a share means.
    domain::HouseholdFacts facts;
    facts.flow = flow;
    const domain::Metrics metrics = domain::computeMetrics(facts);
    shares.present = true;
    shares.buffer = metrics.bufferShareOfSaving;
    shares.investing = metrics.investingShareOfSaving;
    shares.amortisation = metrics.amortisationShareOfSaving;
    return shares;
}

QString deviationSentence(const QString& subject, bool plural, domain::Percentage actual,
                          domain::Percentage target)
{
    const QLocale locale;
    const double actualPoints = static_cast<double>(actual.basisPoints()) / 100.0;
    const double targetPoints = static_cast<double>(target.basisPoints()) / 100.0;
    const int difference = static_cast<int>(std::lround(actualPoints - targetPoints));

    if (difference == 0) {
        const QString targetLabel = locale.toString(targetPoints, 'f', 0);
        return plural ? message("%1 are on the model's %2.").arg(subject, targetLabel)
                      : message("%1 is on the model's %2.").arg(subject, targetLabel);
    }

    const QString points = locale.toString(std::abs(difference));
    const QString targetLabel = locale.toString(targetPoints, 'f', 0);
    if (difference > 0) {
        return plural ? message("%1 are %2 points above %3.").arg(subject, points, targetLabel)
                      : message("%1 is %2 points above %3.").arg(subject, points, targetLabel);
    }
    return plural ? message("%1 are %2 points below %3.").arg(subject, points, targetLabel)
                  : message("%1 is %2 points below %3.").arg(subject, points, targetLabel);
}

QVector<FlowMonth> flowMonthsOf(std::span<const application::MonthFlowRecord> records,
                                domain::YearMonth lastMonth, int count, domain::Currency currency)
{
    QVector<FlowMonth> months;
    if (count <= 0)
        return months;

    months.reserve(count);
    for (int offset = count - 1; offset >= 0; --offset) {
        FlowMonth column;
        column.month = lastMonth.plusMonths(-offset);

        // Records arrive newest first, and a window of twelve months is short
        // enough that a scan per column is clearer than building a map.
        for (const application::MonthFlowRecord& record : records) {
            if (record.month != column.month || record.flow.income.currency() != currency)
                continue;
            const IncomeSplit split = incomeSplitOf(record.flow);
            column.recorded = true;
            column.needsShare = split.needs;
            column.wantsShare = split.wants;
            column.savingShare = split.saving;
            break;
        }

        months.append(column);
    }
    return months;
}

SavingsRateSeries savingsRateSeriesOf(const QVector<FlowMonth>& months)
{
    SavingsRateSeries series;
    series.axisMax = kSavingGoalReference;

    int recorded = 0;
    double highest = 0.0;
    for (const FlowMonth& column : months) {
        if (!column.recorded)
            continue;
        ++recorded;
        highest = std::max(highest, column.savingShare.asFraction());
    }
    if (recorded == 0)
        return series;

    // Two reference lines have to fit, and the line needs air above its peak.
    // Rounded up to whole tens of percent so the axis label is a number a person
    // would have chosen.
    const double scaled = std::ceil(highest * 1.1 * 10.0) / 10.0;
    series.axisMax = domain::Percentage::fromPercent(std::max(0.4, scaled) * 100.0);
    const double axis = series.axisMax.asFraction();

    series.points.reserve(recorded);
    for (int position = 0; position < months.size(); ++position) {
        const FlowMonth& column = months.at(position);
        if (!column.recorded)
            continue;

        ChartPoint point;
        // The x axis is the whole window, not the recorded subset, so a gap in
        // the record shows as a gap in the line rather than as a missing month.
        point.x = months.size() > 1 ? static_cast<double>(position) / (months.size() - 1) : 0.0;
        point.y = axis > 0.0 ? std::clamp(column.savingShare.asFraction() / axis, 0.0, 1.0) : 0.0;
        series.points.append(point);
    }
    series.empty = false;
    return series;
}

} // namespace hinkwise::presentation
