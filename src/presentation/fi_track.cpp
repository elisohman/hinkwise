// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fi_track.h"

#include <algorithm>

namespace hinkwise::presentation {

FiTrack fiTrackOf(const domain::Metrics& metrics, const domain::Jurisdiction& jurisdiction)
{
    FiTrack track;
    track.lowMultiplier = jurisdiction.fiMultiplierLow;
    track.defaultMultiplier = jurisdiction.fiMultiplierDefault;
    track.highMultiplier = jurisdiction.fiMultiplierHigh;
    track.known = metrics.fiTargetDefault.isPositive() && track.defaultMultiplier > 0;
    if (!track.known)
        return track;

    // Every position is a ratio of the default target: the multipliers are the
    // model's band, the progress is the metric it already computed. The axis
    // makes room for the high tick and for a household past x25, then keeps a
    // margin so the last tick is not on the rim.
    const double defaultMultiplier = static_cast<double>(track.defaultMultiplier);
    const double progress = metrics.fiProgress.asFraction();
    const double high = static_cast<double>(track.highMultiplier) / defaultMultiplier;
    const double axis = std::max({1.0, high, progress}) * 1.08;

    track.progressRatio = std::clamp(progress / axis, 0.0, 1.0);
    track.lowRatio =
        std::clamp(static_cast<double>(track.lowMultiplier) / defaultMultiplier / axis, 0.0, 1.0);
    track.defaultRatio = std::clamp(1.0 / axis, 0.0, 1.0);
    track.highRatio = std::clamp(high / axis, 0.0, 1.0);
    return track;
}

} // namespace hinkwise::presentation
