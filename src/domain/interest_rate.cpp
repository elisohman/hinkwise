// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "interest_rate.h"

#include <cmath>
#include <cstdint>

namespace hinkwise::domain {

InterestRate InterestRate::fromPercent(double percent)
{
    return InterestRate{static_cast<std::int32_t>(std::lround(percent * 1000.0))};
}

double InterestRate::asPercent() const noexcept
{
    return static_cast<double>(m_partsPer100k) / 1000.0;
}

Percentage InterestRate::toPercentage() const noexcept
{
    const std::int64_t parts = m_partsPer100k;
    const std::int64_t adjusted = parts >= 0 ? parts + 5 : parts - 5;
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(adjusted / 10));
}

} // namespace hinkwise::domain
