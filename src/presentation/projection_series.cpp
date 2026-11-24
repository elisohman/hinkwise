// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "projection_series.h"

#include "domain/bucket.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hinkwise::presentation {

namespace {

constexpr int kTickCount = 5;

domain::Money tickValue(double minorUnits, domain::Currency currency)
{
    return domain::Money{static_cast<std::int64_t>(minorUnits), currency};
}

} // namespace

ProjectionSeries projectionSeriesOf(const domain::ProjectionInput& input)
{
    ProjectionSeries series;

    const std::vector<domain::ProjectionYear> years = domain::project(input);
    if (years.empty())
        return series;

    const domain::Currency currency = years.front().total.currency();

    // The axis covers whichever is highest: the household's investable capital,
    // its net worth, the debt it carries or the target it is measured against.
    // Net worth belongs in that list because it counts the house and the
    // pensions as well, so it can stand above the bands; the target line and the
    // falling loan belong in it so they are on screen at all. Below zero the
    // axis reaches down to the lowest net worth, because a mortgage larger than
    // everything owned puts net worth there, and a line drawn off the chart is
    // not a line the household can read.
    std::int64_t highest = input.fiTarget.minorUnits();
    std::int64_t lowest = 0;
    for (const domain::ProjectionYear& year : years) {
        highest = std::max(highest, year.total.minorUnits());
        highest = std::max(highest, year.netWorth.minorUnits());
        highest = std::max(highest, year.debt.minorUnits());
        lowest = std::min(lowest, year.netWorth.minorUnits());
    }
    const double top = static_cast<double>(highest) * 1.08;
    const double bottom = lowest < 0 ? static_cast<double>(lowest) * 1.08 : 0.0;
    const double span = top - bottom;
    if (span <= 0.0)
        return series;

    series.valid = true;
    series.hasDebt = !years.front().debt.isZero();
    series.zeroRatio = -bottom / span;
    const auto ratioOfAmount = [&](const domain::Money& amount) {
        return std::clamp((static_cast<double>(amount.minorUnits()) - bottom) / span, 0.0, 1.0);
    };

    series.hasTarget = input.fiTarget.isPositive();
    series.targetRatio = series.hasTarget ? ratioOfAmount(input.fiTarget) : 0.0;

    series.yTicks.reserve(kTickCount);
    for (int tick = 0; tick < kTickCount; ++tick) {
        const double ratio = static_cast<double>(tick) / (kTickCount - 1);
        series.yTicks.append(ProjectionTick{ratio, tickValue(bottom + span * ratio, currency)});
    }

    const int horizon = static_cast<int>(years.size()) - 1;
    series.bands.reserve(years.size());
    series.debt.reserve(years.size());
    series.netWorth.reserve(years.size());
    for (const domain::ProjectionYear& year : years) {
        ProjectionBand band;
        band.yearRatio = horizon > 0 ? static_cast<double>(year.year) / horizon : 0.0;

        // The tops are cumulative, so the band a delegate fills is the space
        // between two of them and the sum of all four is the total line.
        std::int64_t stacked = 0;
        const auto stackedTop = [&](domain::Bucket bucket) {
            stacked += year.perBucket[static_cast<std::size_t>(bucket)].minorUnits();
            return std::clamp((static_cast<double>(stacked) - bottom) / span, 0.0, 1.0);
        };
        band.bufferTop = stackedTop(domain::Bucket::Buffer);
        band.preserveTop = stackedTop(domain::Bucket::Preserve);
        band.growthTop = stackedTop(domain::Bucket::Growth);
        band.playTop = stackedTop(domain::Bucket::Play);
        series.bands.append(band);

        series.debt.append(ProjectionPoint{band.yearRatio, ratioOfAmount(year.debt), year.debt});
        series.netWorth.append(
            ProjectionPoint{band.yearRatio, ratioOfAmount(year.netWorth), year.netWorth});

        if (series.hasDebt && series.yearsToDebtFree < 0 && year.debt.isZero())
            series.yearsToDebtFree = year.year;
    }

    if (series.hasTarget) {
        const std::optional<int> targetYear = domain::yearsToTarget(input);
        if (targetYear.has_value()) {
            series.yearsToTarget = *targetYear;
            series.targetYearRatio = horizon > 0 ? static_cast<double>(*targetYear) / horizon : 0.0;
        }
    }

    return series;
}

} // namespace hinkwise::presentation
