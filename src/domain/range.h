// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "money.h"
#include "percentage.h"

namespace hinkwise::domain {

/// An inclusive interval. The four-bucket model almost never states a single
/// number: it states a band ("0.5-6 months", "25-35 annual expenses") and lets
/// you pick a point inside it based on your situation. Modelling the band
/// explicitly keeps that honesty in the type system.
template <typename T> struct Range {
    T low{};
    T high{};

    bool contains(const T& value) const { return !(value < low) && !(high < value); }

    T clampedTo(const T& value) const
    {
        if (value < low)
            return low;
        if (high < value)
            return high;
        return value;
    }

    bool isEmpty() const { return high < low; }
};

using PercentageRange = Range<Percentage>;
using MoneyRange = Range<Money>;

/// Picks a point inside the band. `tolerance` is the position within the band:
/// 0 % is the cautious end, 100 % the aggressive end.
///
/// The span is computed in 64 bits: two valid basis-point values can differ by
/// more than an int32 holds, and the subtraction would overflow before the
/// widening.
inline Percentage interpolate(const PercentageRange& band, Percentage tolerance)
{
    const std::int64_t span = static_cast<std::int64_t>(band.high.basisPoints()) -
                              static_cast<std::int64_t>(band.low.basisPoints());
    const std::int64_t offset = (span * tolerance.basisPoints()) / 10000;
    return Percentage::fromBasisPoints(band.low.basisPoints() + static_cast<std::int32_t>(offset));
}

inline Money interpolate(const MoneyRange& band, Percentage tolerance)
{
    return band.low + (band.high - band.low).scaledBy(tolerance);
}

} // namespace hinkwise::domain
