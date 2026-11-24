// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "liability.h"

#include <stdexcept>
#include <utility>

namespace hinkwise::domain {

Liability makeLiability(std::string id, std::string name, LiabilityKind kind,
                        InterestRate interestRate, RepaymentStyle style, Money regularPayment,
                        Money monthlyFee, int termMonths)
{
    if (id.empty())
        throw std::invalid_argument{"makeLiability: id must not be empty"};
    if (name.empty())
        throw std::invalid_argument{"makeLiability: name must not be empty"};
    if (interestRate.isNegative())
        throw std::invalid_argument{"makeLiability: interest rate must not be negative"};
    if (regularPayment.isNegative())
        throw std::invalid_argument{"makeLiability: regular payment must not be negative"};
    if (monthlyFee.isNegative())
        throw std::invalid_argument{"makeLiability: monthly fee must not be negative"};
    // A term is a count of months left, so its absence is a zero and not a
    // negative. On a style that has no term the value is ignored: rejecting it
    // would make changing a loan's style have to clear the term first.
    if (termMonths < 0)
        throw std::invalid_argument{"makeLiability: term must not be negative"};
    if (!monthlyFee.isZero() && style != RepaymentStyle::Annuity)
        throw std::invalid_argument{"makeLiability: a monthly fee belongs to an annuity"};

    Liability liability;
    liability.id = std::move(id);
    liability.name = std::move(name);
    liability.kind = kind;
    liability.interestRate = interestRate;
    liability.style = style;
    liability.regularPayment = regularPayment;
    liability.monthlyFee = monthlyFee;
    liability.termMonths = termMonths;
    return liability;
}

Money annualFeeFromMonthly(const Money& monthlyFee)
{
    return Money{monthlyFee.minorUnits() * 12, monthlyFee.currency()};
}

Money monthlyFeeFromAnnual(const Money& annualFee)
{
    std::int64_t scale = 1;
    for (int index = 0; index < traitsOf(annualFee.currency()).minorDigits; ++index)
        scale *= 10;

    const std::int64_t divisor = scale * 12;
    const std::int64_t adjusted = annualFee.minorUnits() >= 0
                                      ? annualFee.minorUnits() + divisor / 2
                                      : annualFee.minorUnits() - divisor / 2;
    return Money{(adjusted / divisor) * scale, annualFee.currency()};
}

bool isBadLoan(const Liability& liability, Percentage mortgageBenchmark)
{
    // Percentage only offers <, so read this as "rate strictly above benchmark".
    return mortgageBenchmark < liability.interestRate.toPercentage();
}

} // namespace hinkwise::domain
