// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "year_month.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace hinkwise::domain {

std::string YearMonth::toString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u", m_year, m_month);
    return std::string{buffer};
}

std::optional<YearMonth> YearMonth::parse(std::string_view text)
{
    if (text.size() != 7 || text[4] != '-')
        return std::nullopt;

    int year = 0;
    unsigned month = 0;

    for (int i = 0; i < 4; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return std::nullopt;
        year = year * 10 + (text[i] - '0');
    }

    for (int i = 5; i < 7; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return std::nullopt;
        month = month * 10 + static_cast<unsigned>(text[i] - '0');
    }

    if (year < kMinYear || year > kMaxYear)
        return std::nullopt;
    if (month < 1 || month > 12)
        return std::nullopt;

    return YearMonth{year, month};
}

} // namespace hinkwise::domain
