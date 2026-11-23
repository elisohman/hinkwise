// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "household.h"

namespace hinkwise::domain {

Money SavingsBreakdown::total() const
{
    return toBuffer + toInvesting + toAmortisation;
}

bool savingSplitMatchesTotal(const MonthFlow& flow)
{
    if (!flow.savingSplit)
        return true;

    const Currency currency = flow.saving.currency();
    const SavingsBreakdown& split = *flow.savingSplit;
    if (split.toBuffer.currency() != currency || split.toInvesting.currency() != currency ||
        split.toAmortisation.currency() != currency)
        return false;

    return split.total() == flow.saving;
}

} // namespace hinkwise::domain
