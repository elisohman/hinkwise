// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "jurisdiction.h"
#include "money.h"
#include "percentage.h"
#include "phase.h"

#include <string>

namespace hinkwise::domain {

/// The household's own declarations: who they are, which currency they think
/// in, where they are on the journey, and which tax constants apply.
///
/// Everything else in the program is derived from these five fields, which is
/// why the profile is a value: it is read at startup, edited as a unit, and
/// stored as one row.
struct Profile {
    std::string displayName;
    Currency baseCurrency = Currency::Sek;
    Phase phase = Phase::BuildingWealth;
    /// Where in the bucket bands the household wants to sit: 0 % cautious,
    /// 100 % bold.
    Percentage riskTolerance{Percentage::fromBasisPoints(5000)};
    Jurisdiction jurisdiction{};
};

} // namespace hinkwise::domain
