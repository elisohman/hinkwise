// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "percentage.h"

#include <cstdint>
#include <string>

namespace hinkwise::domain {

enum class LiabilityKind : std::uint8_t {
    Mortgage,
    StudentLoan,
    ConsumerLoan,
    CreditCard,
    CarLoan,
    Other,
};

struct Liability {
    std::string id;
    std::string name;
    LiabilityKind kind = LiabilityKind::Other;
    Percentage interestRate{};
};

/// A new debt. Throws std::invalid_argument when the id or the name is empty
/// or the rate is negative.
Liability makeLiability(std::string id, std::string name, LiabilityKind kind,
                        Percentage interestRate);

/// The model's framing, not a moral judgement: debt priced above a typical
/// mortgage rate costs more than the mortgage it competes with and should be
/// paid down before anything is invested. Mortgages and student loans are
/// "good" because they are cheap and buy something that lasts; consumer loans
/// and credit cards above the benchmark are "bad". The test is strict:
/// a rate equal to the benchmark is not bad.
bool isBadLoan(const Liability& liability, Percentage mortgageBenchmark);

} // namespace hinkwise::domain
