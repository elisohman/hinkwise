// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace hinkwise::domain {

/// A ratio stored as basis points: 10 000 bp == 100 %.
///
/// Percentages in this domain are always small, exact rational values
/// (savings rates, expected returns, tax drag), so they are stored as integers
/// and never as floating point.
class Percentage {
public:
    constexpr Percentage() noexcept = default;

    static constexpr Percentage fromBasisPoints(std::int32_t basisPoints) noexcept
    {
        return Percentage{basisPoints};
    }

    /// Converts a human percentage (5.5 meaning 5.5 %) to basis points,
    /// rounding half away from zero.
    static Percentage fromPercent(double percent)
    {
        return Percentage{static_cast<std::int32_t>(std::lround(percent * 100.0))};
    }

    constexpr std::int32_t basisPoints() const noexcept { return m_basisPoints; }

    double asFraction() const noexcept { return static_cast<double>(m_basisPoints) / 10000.0; }

    constexpr Percentage operator+(Percentage other) const noexcept
    {
        return Percentage{m_basisPoints + other.m_basisPoints};
    }

    constexpr Percentage operator-(Percentage other) const noexcept
    {
        return Percentage{m_basisPoints - other.m_basisPoints};
    }

    constexpr Percentage operator-() const noexcept { return Percentage{-m_basisPoints}; }

    constexpr bool operator==(const Percentage&) const noexcept = default;

    constexpr bool operator<(Percentage other) const noexcept
    {
        return m_basisPoints < other.m_basisPoints;
    }

    constexpr bool operator>(Percentage other) const noexcept { return other < *this; }

    constexpr bool operator<=(Percentage other) const noexcept { return !(other < *this); }

    constexpr bool operator>=(Percentage other) const noexcept { return !(*this < other); }

    constexpr bool isZero() const noexcept { return m_basisPoints == 0; }
    constexpr bool isNegative() const noexcept { return m_basisPoints < 0; }

private:
    constexpr explicit Percentage(std::int32_t basisPoints) noexcept
        : m_basisPoints(basisPoints)
    {
    }

    std::int32_t m_basisPoints{0};
};

/// Clamps to [low, high].
constexpr Percentage clamp(Percentage value, Percentage low, Percentage high)
{
    if (value < low)
        return low;
    if (high < value)
        return high;
    return value;
}

} // namespace hinkwise::domain
