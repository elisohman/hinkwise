// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "money.h"
#include "year_month.h"

#include <string>

namespace hinkwise::domain {

/// A known future cost: a roof, a car, a sabbatical. Feeds the buffer target,
/// which is sized partly by lump sums due within three years.
///
/// The amount carries its own currency, so a planned expense cannot be added to
/// a balance in another currency by accident.
struct PlannedExpense {
    std::string id;
    std::string name;
    Money amount;
    YearMonth dueMonth{1970, 1};
};

} // namespace hinkwise::domain
