// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "balance_sheet.h"
#include "jurisdiction.h"
#include "money.h"
#include "phase.h"

#include <string>

namespace hinkwise::domain {

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
};

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
    Jurisdiction jurisdiction{};
};

} // namespace hinkwise::domain
