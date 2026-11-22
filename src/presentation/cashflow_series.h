// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/ports.h"
#include "domain/household.h"
#include "domain/percentage.h"
#include "domain/year_month.h"

#include <QString>
#include <QVector>
#include <span>

namespace hinkwise::presentation {

/// The model's 50/30/20 split, as shares of monthly income.
///
/// The domain states the split in its rules and in `docs/model.md` but has no
/// constant for it yet. These three values live here, together, until the model
/// grows one: the cashflow page and the checks that fire on the same split must
/// not be able to disagree about it.
inline constexpr domain::Percentage kNeedsTarget = domain::Percentage::fromBasisPoints(5000);
inline constexpr domain::Percentage kWantsTarget = domain::Percentage::fromBasisPoints(3000);
inline constexpr domain::Percentage kSavingTarget = domain::Percentage::fromBasisPoints(2000);

/// The model's savings-rate line: the 10 % a month the rules call too little,
/// and the 20 % they call the top of the band.
inline constexpr domain::Percentage kSavingFloorReference =
    domain::Percentage::fromBasisPoints(1000);
inline constexpr domain::Percentage kSavingGoalReference =
    domain::Percentage::fromBasisPoints(2000);

/// How one month's income divided between needs, wants and saving.
struct IncomeSplit {
    domain::Percentage needs{};
    domain::Percentage wants{};
    domain::Percentage saving{};
    bool hasIncome = false;
};

/// The split as the domain computes it. A month without income divides nothing
/// and hasIncome says why every share is zero.
IncomeSplit incomeSplitOf(const domain::MonthFlow& flow);

/// The deviation from the model, in plain words: "Needs are 6 points above 50."
/// An exact match says so rather than reporting a deviation of zero points.
/// `plural` picks the verb, because "Needs are" and "Saving is" are both right
/// and a neutral phrasing would be neither.
QString deviationSentence(const QString& subject, bool plural, domain::Percentage actual,
                          domain::Percentage target);

/// One column of the twelve-month history strip.
///
/// `recorded` is what makes a missing month visible: a month with no flow is a
/// blank column, never a bar at zero, because "not recorded" and "recorded as
/// nothing" are different facts about a household.
struct FlowMonth {
    domain::YearMonth month{1970, 1};
    bool recorded = false;
    domain::Percentage needsShare{};
    domain::Percentage wantsShare{};
    domain::Percentage savingShare{};
};

/// `count` months ending at `lastMonth`, oldest first, each with the split of
/// the flow recorded for it. A record in another currency is treated as absent:
/// the columns are compared with each other, so they have to share a unit.
QVector<FlowMonth> flowMonthsOf(std::span<const application::MonthFlowRecord> records,
                                domain::YearMonth lastMonth, int count, domain::Currency currency);

/// A point in a chart: x across the series and y up the axis, both 0..1.
struct ChartPoint {
    double x = 0.0;
    double y = 0.0;
};

/// The savings rate over the months that have one. Only recorded months produce
/// a point; the axis is scaled so both reference lines always fit under the top.
struct SavingsRateSeries {
    QVector<ChartPoint> points;
    /// What the top of the axis stands for.
    domain::Percentage axisMax{};
    bool empty = true;
};

SavingsRateSeries savingsRateSeriesOf(const QVector<FlowMonth>& months);

} // namespace hinkwise::presentation
