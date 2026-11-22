// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hinkwise::domain {

/// A calendar month, used as the key for every monthly flow and valuation.
///
/// The four-bucket model works in monthly and annual units, never in exact
/// dates, so this type is the domain's unit of time.
class YearMonth {
public:
    constexpr YearMonth(int year, unsigned month)
        : m_year(year)
        , m_month(month)
    {
    }

    constexpr int year() const noexcept { return m_year; }
    constexpr unsigned month() const noexcept { return m_month; }

    /// Months since year 0. Used for ordering and arithmetic.
    constexpr std::int64_t index() const noexcept
    {
        return static_cast<std::int64_t>(m_year) * 12 + (m_month - 1);
    }

    static constexpr YearMonth fromIndex(std::int64_t index) noexcept
    {
        const std::int64_t year = index >= 0 ? index / 12 : (index - 11) / 12;
        const std::int64_t month = index - year * 12;
        return YearMonth{static_cast<int>(year), static_cast<unsigned>(month + 1)};
    }

    constexpr YearMonth plusMonths(int months) const noexcept
    {
        return fromIndex(index() + months);
    }

    constexpr YearMonth nextMonth() const noexcept { return plusMonths(1); }

    constexpr bool operator==(const YearMonth&) const noexcept = default;

    constexpr bool operator<(const YearMonth& other) const noexcept
    {
        return index() < other.index();
    }

    constexpr bool operator<=(const YearMonth& other) const noexcept
    {
        return index() <= other.index();
    }

    constexpr bool operator>(const YearMonth& other) const noexcept { return other < *this; }
    constexpr bool operator>=(const YearMonth& other) const noexcept { return other <= *this; }

    /// "YYYY-MM", the canonical storage and comparison format.
    std::string toString() const;

    /// Parses "YYYY-MM". Returns std::nullopt for anything malformed,
    /// including out-of-range months.
    static std::optional<YearMonth> parse(std::string_view text);

    /// Number of months from `from` to `to`; negative if `to` precedes `from`.
    static std::int64_t monthsBetween(YearMonth from, YearMonth to) noexcept
    {
        return to.index() - from.index();
    }

private:
    int m_year{1970};
    unsigned m_month{1};
};

} // namespace hinkwise::domain
