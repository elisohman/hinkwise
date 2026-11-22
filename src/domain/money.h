// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "percentage.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace hinkwise::domain {

/// Currencies hinkwise supports as a single base currency per profile.
/// Deliberately an enum rather than free-form strings: every code path that
/// touches money must agree on the currency, and the compiler enforces it.
enum class Currency : std::uint8_t {
    Sek,
    Eur,
    Usd,
};

struct CurrencyTraits {
    const char* isoCode;
    int minorDigits;
};

constexpr CurrencyTraits traitsOf(Currency currency) noexcept
{
    switch (currency) {
    case Currency::Sek:
        return {"SEK", 2};
    case Currency::Eur:
        return {"EUR", 2};
    case Currency::Usd:
        return {"USD", 2};
    }
    return {"???", 2};
}

constexpr const char* isoCodeOf(Currency currency) noexcept
{
    return traitsOf(currency).isoCode;
}

/// An exact amount of money, stored as integer minor units (öre, cents).
///
/// Floating point is never used for money: 0.1 + 0.2 != 0.3 is not acceptable
/// in something that tracks a household's economy. Formatting for humans
/// happens at the presentation edge, never here.
///
/// Arithmetic between different currencies is a programming error and throws.
class Money {
public:
    constexpr Money() noexcept = default;

    constexpr Money(std::int64_t minorUnits, Currency currency) noexcept
        : m_minorUnits(minorUnits)
        , m_currency(currency)
    {
    }

    static constexpr Money zero(Currency currency) noexcept { return Money(0, currency); }

    constexpr std::int64_t minorUnits() const noexcept { return m_minorUnits; }
    constexpr Currency currency() const noexcept { return m_currency; }

    constexpr bool isZero() const noexcept { return m_minorUnits == 0; }
    constexpr bool isNegative() const noexcept { return m_minorUnits < 0; }
    constexpr bool isPositive() const noexcept { return m_minorUnits > 0; }

    Money operator+(const Money& other) const
    {
        requireSameCurrency(other);
        return Money{m_minorUnits + other.m_minorUnits, m_currency};
    }

    Money operator-(const Money& other) const
    {
        requireSameCurrency(other);
        return Money{m_minorUnits - other.m_minorUnits, m_currency};
    }

    constexpr Money operator-() const noexcept { return Money{-m_minorUnits, m_currency}; }

    Money& operator+=(const Money& other)
    {
        requireSameCurrency(other);
        m_minorUnits += other.m_minorUnits;
        return *this;
    }

    Money& operator-=(const Money& other)
    {
        requireSameCurrency(other);
        m_minorUnits -= other.m_minorUnits;
        return *this;
    }

    constexpr bool operator==(const Money& other) const noexcept = default;

    bool operator<(const Money& other) const
    {
        requireSameCurrency(other);
        return m_minorUnits < other.m_minorUnits;
    }

    bool operator<=(const Money& other) const
    {
        requireSameCurrency(other);
        return m_minorUnits <= other.m_minorUnits;
    }

    bool operator>(const Money& other) const { return other < *this; }
    bool operator>=(const Money& other) const { return other <= *this; }

    /// Scales by a percentage, rounding half away from zero.
    Money scaledBy(Percentage percentage) const noexcept
    {
        const std::int64_t product = m_minorUnits * percentage.basisPoints();
        const std::int64_t adjusted = product >= 0 ? product + 5000 : product - 5000;
        return Money{adjusted / 10000, m_currency};
    }

    /// Splits this amount into `parts` shares, distributing the remainder so
    /// that the shares sum back to exactly this amount. Used when a monthly
    /// contribution has to be spread across buckets without losing an öre.
    std::int64_t splitShare(int index, int parts) const
    {
        if (parts <= 0 || index < 0 || index >= parts)
            throw std::invalid_argument{"Money::splitShare: bad index or parts"};

        const std::int64_t base = m_minorUnits / parts;
        const std::int64_t remainder = m_minorUnits - base * parts;
        const std::int64_t step = remainder >= 0 ? 1 : -1;
        const std::int64_t spread = remainder >= 0 ? remainder : -remainder;
        return base + (index < spread ? step : 0);
    }

    /// "1234.56 SEK" — for logs and tests only. The UI formats with QLocale.
    std::string toString() const
    {
        const bool negative = m_minorUnits < 0;
        const std::int64_t magnitude = negative ? -m_minorUnits : m_minorUnits;
        const std::int64_t major = magnitude / 100;
        const int minor = static_cast<int>(magnitude % 100);

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s%lld.%02d %s", negative ? "-" : "",
                      static_cast<long long>(major), minor, isoCodeOf(m_currency));
        return std::string{buffer};
    }

private:
    void requireSameCurrency(const Money& other) const
    {
        if (m_currency != other.m_currency)
            throw std::invalid_argument{"Money: currency mismatch"};
    }

    std::int64_t m_minorUnits{0};
    Currency m_currency{Currency::Sek};
};

} // namespace hinkwise::domain
