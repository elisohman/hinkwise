// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/money.h"
#include "domain/projection.h"

#include <QVector>

namespace hinkwise::presentation {

/// One year of the projection as a chart draws it: where it sits along the
/// horizon, and the four bucket tops stacked up one shared axis. Each top is
/// the running total of everything below it, so a delegate draws the band
/// between two tops and never adds anything itself.
struct ProjectionBand {
    double yearRatio = 0.0;
    double bufferTop = 0.0;
    double preserveTop = 0.0;
    double growthTop = 0.0;
    double playTop = 0.0;
};

/// A horizontal gridline: where it sits on the axis, and the money it stands
/// for, already an amount so the caller only formats it.
struct ProjectionTick {
    double ratio = 0.0;
    domain::Money value{};
};

/// What the projection chart and its markings need from domain::project().
///
/// The yearly steps are the domain's; this only turns them into positions. The
/// axis leaves a margin above the highest point so the last year is not drawn
/// on the rim, and it makes room for the FI target line even when the household
/// does not reach it within the horizon.
struct ProjectionSeries {
    QVector<ProjectionBand> bands;
    QVector<ProjectionTick> yTicks;
    bool hasTarget = false;
    double targetRatio = 0.0;
    /// Year in which the FI target is first reached, -1 when never within the
    /// horizon or when the household states no target.
    int yearsToTarget = -1;
    double targetYearRatio = 0.0;
    /// False when there is nothing to draw: no years, or every figure is zero.
    bool valid = false;
};

ProjectionSeries projectionSeriesOf(const domain::ProjectionInput& input);

} // namespace hinkwise::presentation
