// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "household.h"
#include "insight.h"
#include "metrics.h"

#include <optional>
#include <string_view>
#include <vector>

namespace hinkwise::domain {

/// One check the model would make, expressed as a small object.
///
/// Rules are separate types rather than a switch, so each one can be tested in
/// isolation with a hand-built HouseholdFacts and a hand-built Metrics, and so
/// adding a rule never means editing a growing if-chain.
class Rule
{
public:
    virtual ~Rule() = default;

    virtual std::string_view id() const = 0;
    virtual std::optional<Insight> evaluate(const HouseholdFacts& facts,
                                            const Metrics& metrics) const = 0;
};

/// Runs every rule and returns what fired, most urgent first.
std::vector<Insight> evaluateRules(const HouseholdFacts& facts, const Metrics& metrics);

} // namespace hinkwise::domain
