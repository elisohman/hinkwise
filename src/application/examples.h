// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/profile.h"
#include "domain/year_month.h"
#include "ports.h"
#include "result.h"

#include <cstdint>

namespace hinkwise::application {

/// The two example households settings can create.
///
/// An example is a whole financial story in one profile: twelve recorded
/// months, the holdings the story talks about and the balances that go with
/// them, so every screen has real figures to show from the first minute. It is
/// flagged as an example - see domain::Profile::isExample - because figures the
/// household did not enter must never be mistaken for its own.
enum class ExampleProfile : std::uint8_t {
    /// A household in its accumulating years: a buffer, index funds, a flat
    /// with a mortgage, income pension and a little play money.
    BuildingWealth = 0,
    /// The same household later: the buffer and preserve buckets carry the
    /// living costs and there is no debt left.
    Preserving = 1,
};

/// The profile an example starts as: named for what it shows, in the phase its
/// figures were built for, flagged as an example and keeping a monthly record,
/// because that is what twelve recorded months are. The id is zero: the caller
/// stores the row and owns the result.
domain::Profile exampleProfile(ExampleProfile kind);

/// Writes one example's holdings, twelve recorded months, this month's
/// valuations and this month's debt balances through `repositories`.
///
/// The repositories must already be pointed at the profile that will own the
/// rows, and the caller must already have opened the transaction they belong
/// in: a half-written example is a household with figures that do not add up.
///
/// `profileId` prefixes every asset, liability and expense id, because those
/// ids are unique across the whole file and two copies of the same example have
/// to be able to live in it side by side.
Status seedExample(ExampleProfile kind, std::int64_t profileId, IAssetRepository& assets,
                   ILiabilityRepository& liabilities, IMonthFlowRepository& flows,
                   IValuationRepository& valuations, IDebtBalanceRepository& debtBalances,
                   domain::YearMonth currentMonth);

} // namespace hinkwise::application
