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

    // The axis covers whichever is higher, the household's own fortune or the
    // target it is measured against, so the target line is always on screen.
    std::int64_t highest = input.fiTarget.minorUnits();
    for (const domain::ProjectionYear& year : years)
        highest = std::max(highest, year.total.minorUnits());
    const double axis = static_cast<double>(highest) * 1.08;
    if (axis <= 0.0)
        return series;

    series.valid = true;
    series.hasTarget = input.fiTarget.isPositive();
    series.targetRatio =
        series.hasTarget ? static_cast<double>(input.fiTarget.minorUnits()) / axis : 0.0;

    series.yTicks.reserve(kTickCount);
    for (int tick = 0; tick < kTickCount; ++tick) {
        const double ratio = static_cast<double>(tick) / (kTickCount - 1);
        series.yTicks.append(ProjectionTick{ratio, tickValue(axis * ratio, currency)});
    }

    const int horizon = static_cast<int>(years.size()) - 1;
    series.bands.reserve(years.size());
    for (const domain::ProjectionYear& year : years) {
        ProjectionBand band;
        band.yearRatio = horizon > 0 ? static_cast<double>(year.year) / horizon : 0.0;

        // The tops are cumulative, so the band a delegate fills is the space
        // between two of them and the sum of all four is the total line.
        std::int64_t stacked = 0;
        const auto top = [&](domain::Bucket bucket) {
            stacked += year.perBucket[static_cast<std::size_t>(bucket)].minorUnits();
            return std::clamp(static_cast<double>(stacked) / axis, 0.0, 1.0);
        };
        band.bufferTop = top(domain::Bucket::Buffer);
        band.preserveTop = top(domain::Bucket::Preserve);
        band.growthTop = top(domain::Bucket::Growth);
        band.playTop = top(domain::Bucket::Play);
        series.bands.append(band);
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
