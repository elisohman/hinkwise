// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/update_flow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace hinkwise::application {

std::vector<domain::YearMonth> recordableMonths(domain::YearMonth current, int count)
{
    std::vector<domain::YearMonth> months;
    if (count <= 0)
        return months;

    months.reserve(static_cast<std::size_t>(count));
    // The current month is always offered, so a count that would run past the
    // first representable month stops there rather than throwing.
    const std::int64_t floor = domain::YearMonth::kMinYear * 12;
    for (int index = 0; index < count; ++index) {
        const std::int64_t monthIndex = current.index() - index;
        if (monthIndex < floor)
            break;
        months.push_back(domain::YearMonth::fromIndex(monthIndex));
    }
    return months;
}

std::optional<MoneyPoint> amountInEffectAt(std::span<const MoneyPoint> history,
                                           domain::YearMonth month)
{
    // The history is oldest first, so the last point that is not after `month`
    // is the one in effect.
    const auto found =
        std::find_if(history.rbegin(), history.rend(),
                     [&month](const MoneyPoint& point) { return point.month <= month; });
    if (found == history.rend())
        return std::nullopt;
    return *found;
}

} // namespace hinkwise::application
