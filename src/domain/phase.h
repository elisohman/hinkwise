// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "targets.h"

#include <array>
#include <cstdint>

namespace hinkwise::domain {

/// Where a household is on its economic journey. This is the one thing the
/// user declares; everything else (targets, flow, rules, headline metric) is
/// derived from it by the matching PhasePolicy.
enum class Phase : std::uint8_t {
    GettingStarted = 0,
    BuildingWealth = 1,
    Preserving = 2,
    FinanciallyIndependent = 3,
};

inline constexpr std::array<Phase, 4> allPhases()
{
    return {Phase::GettingStarted, Phase::BuildingWealth, Phase::Preserving,
            Phase::FinanciallyIndependent};
}

const char* labelOf(Phase phase);

/// Which way money moves through the buckets. In the accumulating phases the
/// buckets fill; in the preserving phases the buffer is drawn down and refilled
/// from the preserve bucket. Reversing this is the single most visible effect
/// of changing phase, and the reason phase cannot be a cosmetic setting.
enum class FlowDirection {
    IntoBuckets,
    OutOfBuckets,
};

enum class HeadlineMetric {
    YearsOfFreedom,     ///< investable capital divided by annual expenses
    SafeWithdrawalRate, ///< what the capital can sustainably pay out per year
};

/// Everything that differs between phases, in one object.
///
/// The UI never switches on Phase. It asks the policy and renders the answer,
/// so there is exactly one place where phase behaviour is defined.
class PhasePolicy {
public:
    virtual ~PhasePolicy() = default;

    virtual Phase phase() const = 0;
    virtual const BucketTargets& targets() const = 0;
    virtual FlowDirection flow() const = 0;
    virtual HeadlineMetric headline() const = 0;
};

/// The only switch that decides behaviour. labelOf() also switches, but only
/// to produce a name.
const PhasePolicy& policyFor(Phase phase);

/// True for the phases that fill the buckets rather than draw them down.
bool isAccumulating(Phase phase);

} // namespace hinkwise::domain
