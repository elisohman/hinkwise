// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "jurisdiction.h"
#include "money.h"
#include "percentage.h"
#include "phase.h"

#include <cstdint>
#include <string>

namespace hinkwise::domain {

/// How the household keeps its record, and therefore what an old month means.
///
/// Accounting is bookkeeping: every month is written down, and a month that was
/// missed is a hole in the record to be filled. Planning is a standing budget:
/// the inputs are assumptions that stay true until something real changes - the
/// salary, a new debt, a large investment - so a figure from last spring is
/// expected rather than neglected.
///
/// The two are the same data. A budget is simply the most recent recorded
/// month; the mode decides how the application talks about its age.
enum class TrackingMode : std::uint8_t {
    Accounting = 0,
    Planning = 1,
};

const char* labelOf(TrackingMode mode);

/// The household's own declarations: who they are, which currency they think
/// in, where they are on the journey, which tax constants apply, and how they
/// keep their record.
///
/// Everything else in the program is derived from these fields, which is why the
/// profile is a value: it is read at startup, edited as a unit, and stored as
/// one row.
struct Profile {
    /// The row this profile was read from, zero before it has been stored.
    /// The application layer carries it so a saved profile edits the same
    /// household rather than becoming a second one.
    std::int64_t id = 0;
    std::string displayName;
    Currency baseCurrency = Currency::Sek;
    Phase phase = Phase::BuildingWealth;
    /// Where in the bucket bands the household wants to sit: 0 % cautious,
    /// 100 % bold.
    Percentage riskTolerance{Percentage::fromBasisPoints(5000)};
    Jurisdiction jurisdiction{};
    /// Planning by default: most households do not sit down with their
    /// balances every month, and a fresh household greeted with staleness
    /// warnings on its second day would be told off for a choice it never made.
    TrackingMode trackingMode = TrackingMode::Planning;
    /// True for a profile the application filled with an example rather than
    /// the household itself. The flag exists so every screen can say so and the
    /// household can delete it in one action: example figures that cannot be
    /// told apart from real ones are worse than no example at all.
    bool isExample = false;
};

} // namespace hinkwise::domain
