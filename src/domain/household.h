// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "balance_sheet.h"
#include "jurisdiction.h"
#include "money.h"
#include "phase.h"

#include <optional>
#include <string>

namespace hinkwise::domain {

/// How a month's saving was divided between the three places the model sends
/// it. This is a breakdown, never a second total: the month's `saving` stays
/// the authoritative figure and the parts have to add up to it.
struct SavingsBreakdown {
    Money toBuffer;
    Money toInvesting;
    Money toAmortisation;

    /// The three parts added up. Throws when a part is in another currency,
    /// like every other Money addition: that is a programming error, and a
    /// breakdown of one amount is one currency by definition.
    Money total() const;
};

/// One month of income and spending, as the household records it.
///
/// `saving` includes amortisation: the model counts paying down good debt as
/// saving, which is why "pay yourself first, 10-20 % including amortisation"
/// is a coherent instruction.
struct MonthFlow {
    Money income;
    Money needs;
    Money wants;
    Money saving;

    /// Where `saving` went, when the household recorded that. Optional because
    /// months written before the breakdown existed carry only the total, and
    /// because a household that knows only the total has said nothing about the
    /// division: inventing one would be a lie.
    ///
    /// Defaulted rather than left to zero-initialisation so a flow built from
    /// the four amounts alone is still a complete, consistent value.
    std::optional<SavingsBreakdown> savingSplit = std::nullopt;
};

/// True when the flow agrees with itself: either it carries no breakdown, or
/// the three parts add up to its saving in the same currency.
///
/// The boundaries that read a flow from outside - the repository and the JSON
/// backup - refuse a flow where this is false rather than passing on a
/// breakdown that contradicts its own total.
bool savingSplitMatchesTotal(const MonthFlow& flow);

/// Everything the calculations and the rules need, and nothing else.
///
/// Built once per screen refresh from the profile, the balance sheet, the
/// resolved targets and the last month's flow. It is a value: pass it by
/// const reference, never mutate it, and two consumers cannot disagree.
struct HouseholdFacts {
    Phase phase = Phase::BuildingWealth;
    BalanceSheet sheet{};
    ResolvedTargets targets{};
    MonthFlow flow{};
    Money badLoanTotal{};
    /// Everything held as RealEstate, at market value whatever bucket it sits
    /// in. Kept beside the sheet because a rule compares it with the mortgage
    /// total and the sheet only aggregates by bucket.
    Money realEstateValue{};
    /// Every Mortgage liability added up, for the same reason.
    Money mortgageTotal{};
    Jurisdiction jurisdiction{};
};

} // namespace hinkwise::domain
