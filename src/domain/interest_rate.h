// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "percentage.h"

#include <cstdint>

namespace hinkwise::domain {

/// A loan's annual interest rate stored in hundred-thousandths of a whole:
/// 100 000 == 100.000 %, so one unit is 0.001 %.
///
/// A lender quotes a loan to three decimals, and rounding that to the two a
/// Percentage holds would change the rate the household typed. Percentage stays
/// at basis points for returns, tax, tolerance and shares, where two decimals
/// are the right unit; this type carries the loan rate on its own.
class InterestRate {
public:
    constexpr InterestRate() noexcept = default;

    static constexpr InterestRate fromPartsPer100k(std::int32_t partsPer100k) noexcept
    {
        return InterestRate{partsPer100k};
    }

    /// Converts a human percentage (2.655 meaning 2.655 %) to parts per 100 000,
    /// rounding half away from zero.
    static InterestRate fromPercent(double percent);

    constexpr std::int32_t partsPer100k() const noexcept { return m_partsPer100k; }

    double asPercent() const noexcept;

    /// The rate in basis points, for the one comparison that speaks Percentage:
    /// the mortgage benchmark. Ten parts per 100 000 make one basis point, so
    /// this rounds half away from zero and a rate between two basis points
    /// lands on one of them. A comparison made this way is at basis-point
    /// granularity, not at the rate's own three decimals.
    Percentage toPercentage() const noexcept;

    constexpr bool operator==(const InterestRate&) const noexcept = default;

    constexpr bool operator<(InterestRate other) const noexcept
    {
        return m_partsPer100k < other.m_partsPer100k;
    }

    constexpr bool isZero() const noexcept { return m_partsPer100k == 0; }
    constexpr bool isNegative() const noexcept { return m_partsPer100k < 0; }

private:
    constexpr explicit InterestRate(std::int32_t partsPer100k) noexcept
        : m_partsPer100k(partsPer100k)
    {
    }

    std::int32_t m_partsPer100k{0};
};

} // namespace hinkwise::domain
