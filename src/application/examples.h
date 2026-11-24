// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/profile.h"
#include "domain/year_month.h"
#include "ports.h"
#include "result.h"

#include <cstdint>

namespace hinkwise::application {

/// The four example households settings can create, in phase order.
///
/// An example is a whole financial story in one profile: twelve recorded
/// months, the holdings the story talks about and the balances that go with
/// them, so every screen has real figures to show from the first minute. It is
/// flagged as an example - see domain::Profile::isExample - because figures the
/// household did not enter must never be mistaken for its own.
enum class ExampleProfile : std::uint8_t {
    /// A household that has just started: the buffer is still below its band,
    /// the first index fund is in growth and the occupational pension promise
    /// sits in preserve.
    GettingStarted = 0,
    /// A household in its accumulating years: a buffer, index funds, a flat
    /// with a mortgage, income pension and a little play money.
    BuildingWealth = 1,
    /// The same household later: the buffer and preserve buckets carry the
    /// living costs, growth is optional and there is no debt left.
    Preserving = 2,
    /// The end state of the model: five years of cash and twenty-five years of
    /// preserve, with growth and play gone and the property sold.
    FinanciallyIndependent = 3,
};

/// The profile an example starts as: named for what it shows, in the phase its
/// figures were built for, flagged as an example and keeping the record the
/// story is told in. The id is zero: the caller stores the row and owns the
/// result.
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
