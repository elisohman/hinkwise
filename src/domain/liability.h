// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "money.h"
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

/// How a loan's recorded monthly payment is meant.
///
/// The two styles differ in what `regularPayment` stands for, which is why the
/// style has to travel with the amount: a Swedish student loan is repaid as one
/// fixed monthly sum that covers interest, an administration fee and
/// amortisation together, while a mortgage payment is usually stated as the
/// principal part alone and the interest is charged on top.
enum class RepaymentStyle : std::uint8_t {
    /// `regularPayment` is the principal repaid each month. Interest is a cost
    /// beside it.
    FixedAmortisation,
    /// `regularPayment` is the whole monthly payment: interest, the
    /// administration fee and whatever principal is left over.
    Annuity,
};

struct Liability {
    std::string id;
    std::string name;
    LiabilityKind kind = LiabilityKind::Other;
    Percentage interestRate{};
    RepaymentStyle style = RepaymentStyle::FixedAmortisation;
    /// MONTHLY. For FixedAmortisation this is the principal repaid; for Annuity
    /// it is the total paid, interest and fee included. Zero means no terms are
    /// recorded yet, which is not the same as paying nothing towards a loan the
    /// household has: the plan says so rather than drawing an invented paydown.
    Money regularPayment{};
    /// MONTHLY, Annuity only: an administration fee charged each month, a cost
    /// and never principal. Zero for every other style.
    Money monthlyFee{};
    /// Months the loan still has to run from today. Zero means "not stated",
    /// which keeps the recorded regularPayment in force rather than replacing
    /// it with one the term implies. A Swedish student loan's term is the part
    /// that does not move: CSN keeps it and recalculates the payment each
    /// January, so the term is what a loan that recalculates is described by.
    /// Only an annuity reads it.
    int termMonths = 0;
};

/// A new debt. Throws std::invalid_argument when the id or the name is empty,
/// the rate is negative, the payment or the fee is negative, the term is
/// negative, or a fee is set on a style that has no fee.
Liability makeLiability(std::string id, std::string name, LiabilityKind kind,
                        Percentage interestRate,
                        RepaymentStyle style = RepaymentStyle::FixedAmortisation,
                        Money regularPayment = {}, Money monthlyFee = {}, int termMonths = 0);

/// The model's framing, not a moral judgement: debt priced above a typical
/// mortgage rate costs more than the mortgage it competes with and should be
/// paid down before anything is invested. Mortgages and student loans are
/// "good" because they are cheap and buy something that lasts; consumer loans
/// and credit cards above the benchmark are "bad". The test is strict:
/// a rate equal to the benchmark is not bad.
bool isBadLoan(const Liability& liability, Percentage mortgageBenchmark);

} // namespace hinkwise::domain
