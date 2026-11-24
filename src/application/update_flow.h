// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/money.h"
#include "domain/year_month.h"
#include "ports.h"

#include <optional>
#include <span>
#include <vector>

namespace hinkwise::application {

/// How many months back the update flow offers: the current month and the
/// sixty before it. Far enough to fill in a missed year in either tracking
/// mode, and bounded so the picker stays a list instead of a calendar.
inline constexpr int kRecordableMonths = 61;

/// The months a household may record into, newest first: `count` months ending
/// at `current`. Never a future one - the entry for a month that has not
/// happened yet is not a late entry, it is a wrong one.
std::vector<domain::YearMonth> recordableMonths(domain::YearMonth current, int count);

/// The amount the record already implies for `month`: the last point at or
/// before it, or std::nullopt when the history holds nothing that early.
///
/// This is what a balance step starts its fields from. It is deliberately not
/// the latest point: recording January after March must not carry March's
/// figure back into it. `history` is oldest first, which is how
/// IValuationRepository::history() returns it.
std::optional<MoneyPoint> amountInEffectAt(std::span<const MoneyPoint> history,
                                           domain::YearMonth month);

} // namespace hinkwise::application
