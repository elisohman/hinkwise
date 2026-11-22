// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/jurisdiction.h"
#include "domain/metrics.h"

namespace hinkwise::presentation {

/// Where the household sits on the FI track: progress toward the default
/// x-multiple, and the x21 / x25 / x33 band as positions on the same 0..1 axis,
/// so a bar only multiplies them by pixels. The multipliers travel with the
/// positions because the label under each tick is the model's, not a guess made
/// from the position.
struct FiTrack {
    bool known = false;
    double progressRatio = 0.0;
    double lowRatio = 0.0;
    double defaultRatio = 0.0;
    double highRatio = 0.0;
    int lowMultiplier = 0;
    int defaultMultiplier = 0;
    int highMultiplier = 0;
};

/// The track from the metrics the domain already computed and the jurisdiction
/// that supplied the multipliers. A zero target means the household states no
/// target - no expenses have been recorded - and the track is not drawn at all.
FiTrack fiTrackOf(const domain::Metrics& metrics, const domain::Jurisdiction& jurisdiction);

} // namespace hinkwise::presentation
