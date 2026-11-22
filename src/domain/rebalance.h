// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bucket.h"
#include "money.h"
#include "phase.h"
#include "targets.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace hinkwise::domain {

/// One movement between two buckets. The amount is always positive and a
/// bucket never pays itself: a proposal only contains moves worth making.
struct Transfer {
    /// The model's most common move is out of a full bucket into Growth.
    Bucket from = Bucket::Buffer;
    Bucket to = Bucket::Growth;
    Money amount{};
};

/// Everything the planner looks at: where the household is in its journey,
/// what the four buckets hold, what the phase says they should hold, and
/// whether the market is falling right now.
struct RebalanceInput {
    Phase phase = Phase::BuildingWealth;
    /// Indexed by Bucket.
    std::array<Money, 4> balances{};
    ResolvedTargets targets{};
    /// Free money this month, after expenses and saving. Zero is normal, and
    /// it never changes the plan: a proposal says where the money goes, not how
    /// much of it there is. It is here because the caller has the figure and a
    /// screen wants to show it next to the advice.
    Money monthlySurplus{};
    /// The model's rule: never sell growth into a fall. The caller sets this
    /// from whatever it knows about the market; the planner only obeys it.
    bool marketDown = false;
};

/// What to do next, in the model's own terms.
struct RebalanceProposal {
    /// Where the next free krona should go. Empty in the preserving phases,
    /// where there is no free money to allocate by design.
    std::optional<Bucket> nextContribution;
    /// Moves to make now, in the order the model would make them.
    std::vector<Transfer> transfers;
    /// One sentence, plain language: why this plan and not another.
    std::string rationale;
};

/// The plan for one month. Accumulating phases keep filling the buckets;
/// preserving phases keep the buffer inside its band by drawing on Preserve.
RebalanceProposal planRebalance(const RebalanceInput& input);

} // namespace hinkwise::domain
