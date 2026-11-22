// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "liability.h"

#include <stdexcept>
#include <utility>

namespace hinkwise::domain {

Liability makeLiability(std::string id, std::string name, LiabilityKind kind,
                        Percentage interestRate)
{
    if (id.empty())
        throw std::invalid_argument{"makeLiability: id must not be empty"};
    if (name.empty())
        throw std::invalid_argument{"makeLiability: name must not be empty"};
    if (interestRate.isNegative())
        throw std::invalid_argument{"makeLiability: interest rate must not be negative"};

    Liability liability;
    liability.id = std::move(id);
    liability.name = std::move(name);
    liability.kind = kind;
    liability.interestRate = interestRate;
    return liability;
}

bool isBadLoan(const Liability& liability, Percentage mortgageBenchmark)
{
    // Percentage only offers <, so read this as "rate strictly above benchmark".
    return mortgageBenchmark < liability.interestRate;
}

} // namespace hinkwise::domain
