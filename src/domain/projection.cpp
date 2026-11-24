// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "projection.h"

#include "bucket.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hinkwise::domain {

namespace {

/// Basis points in a whole: 100 % of a target, or one year of expenses.
constexpr std::int64_t basisPointsInWhole = 10000;

/// Months in a year, and therefore the divisor that turns an annual rate into
/// the rate one repayment is charged at.
constexpr std::int64_t monthsPerYear = 12;

/// numerator / denominator in basis points, rounded half away from zero.
///
/// Kept local because the domain has no shared ratio header yet, but identical
/// to ratioOf in metrics.cpp on purpose: two ways of dividing by expenses in one
/// program would eventually disagree on a rounded basis point, and neither
/// answer would be obviously wrong. A denominator that is zero or negative has
/// no meaningful ratio, so the answer is zero rather than a division by it.
Percentage ratioOf(const Money& numerator, const Money& denominator)
{
    const std::int64_t divisor = denominator.minorUnits();
    if (divisor <= 0)
        return Percentage::fromBasisPoints(0);

    const std::int64_t product = numerator.minorUnits() * basisPointsInWhole;
    const std::int64_t adjusted = product >= 0 ? product + divisor / 2 : product - divisor / 2;
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(adjusted / divisor));
}

Money sumOf(const std::array<Money, 4>& balances)
{
    Money total = balances.front();
    for (std::size_t index = 1; index < balances.size(); ++index)
        total += balances[index];
    return total;
}

/// One month of interest: the annual rate spread over the twelve repayments of
/// a year, in integer minor units, rounded half away from zero. A negative rate
/// is broken input rather than a rebate, so it earns nothing.
Money monthlyInterest(const Money& balance, Percentage annualRate)
{
    if (!balance.isPositive() || annualRate.isNegative())
        return Money::zero(balance.currency());

    const std::int64_t divisor = basisPointsInWhole * monthsPerYear;
    const std::int64_t product = balance.minorUnits() * annualRate.basisPoints();
    const std::int64_t adjusted = product >= 0 ? product + divisor / 2 : product - divisor / 2;
    return Money{adjusted / divisor, balance.currency()};
}

/// The rate one month is charged at, as the fraction the annuity factor needs.
double monthlyRateOf(Percentage annualRate)
{
    return static_cast<double>(annualRate.basisPoints()) /
           static_cast<double>(basisPointsInWhole * monthsPerYear);
}

/// One loan while the projection runs: its terms, the balance left after the
/// months simulated so far, and the payment the current year is charged at.
struct LoanState {
    LoanTerms terms;
    Money balance;
    /// Months of the loan's term not yet simulated. Zero for a loan that states
    /// no term, which is never counted down.
    int monthsRemaining = 0;
    /// The payment in force. The recorded one unless the loan is an annuity
    /// with a term, in which case renewPayments() recalculates it.
    Money paymentInForce{};
};

std::vector<LoanState> statesOf(const std::vector<LoanTerms>& loans)
{
    std::vector<LoanState> states;
    states.reserve(loans.size());
    for (const LoanTerms& terms : loans) {
        // A negative balance is not a debt the model can reason about, so it
        // starts where a repaid loan does.
        const Money balance =
            terms.balance.isNegative() ? Money::zero(terms.balance.currency()) : terms.balance;
        const bool termCounts = terms.style == RepaymentStyle::Annuity && terms.termMonths > 0;
        states.push_back(
            LoanState{terms, balance, termCounts ? terms.termMonths : 0, terms.regularPayment});
    }
    return states;
}

/// Recalculates the payment of every loan whose repayment is set by a term.
///
/// A Swedish student loan is an annuity whose payment CSN holds for one
/// calendar year and recalculates each January from the balance left, the
/// months left and the rate. The payment is therefore recomputed here - once,
/// at the first month of every projection year - and held for that year's
/// twelve months. A loan with no term keeps the payment it was recorded with.
///
/// With one rate held for the whole projection the recalculation returns very
/// nearly the same payment every January, because the balance and the months
/// left fall in step. A real CSN payment does fall year on year: CSN rounds it
/// up to whole kronor, so the loan amortises a little faster than the schedule,
/// and it resets the rate each year. Neither is modelled here, and pretending
/// otherwise would be a worse answer than stating it.
void renewPayments(std::vector<LoanState>& loans)
{
    for (LoanState& loan : loans) {
        if (loan.terms.style != RepaymentStyle::Annuity || loan.terms.termMonths <= 0)
            continue;
        loan.paymentInForce = annuityPayment(loan.balance, loan.terms.annualRate,
                                             loan.monthsRemaining, loan.terms.monthlyFee);
    }
}

/// What one simulated year cost and cleared.
struct YearRepayment {
    Money interestPaid;
    Money feesPaid;
    bool loanNeverRepaid = false;
};

/// Repays one loan for one month, adding what it cost to the year's totals.
///
/// FixedAmortisation: the recorded payment is principal, so the whole of it
/// comes off the balance and the interest is a separate cost.
///
/// Annuity: the payment in force is everything, so the principal is whatever is
/// left after interest and the administration fee. As the balance falls the
/// interest share falls with it and the principal share rises, which is exactly
/// the behaviour a yearly approximation gets wrong.
///
/// An annuity whose payment does not cover interest plus fee cannot reduce its
/// balance. Growing it forever would turn one broken figure into a debt that
/// swallows the projection, so the balance is kept flat and the year is flagged
/// for the interface to explain. A flat balance does not consume the term
/// either: nothing is being amortised, so there is no month of the loan's life
/// to count down.
void repayOneMonth(LoanState& loan, YearRepayment& year)
{
    if (!loan.balance.isPositive())
        return;

    const Money interest = monthlyInterest(loan.balance, loan.terms.annualRate);
    year.interestPaid += interest;

    if (loan.terms.style == RepaymentStyle::Annuity) {
        const Money fee = loan.terms.monthlyFee.isNegative() ? Money::zero(loan.balance.currency())
                                                             : loan.terms.monthlyFee;
        year.feesPaid += fee;
        const Money cost = interest + fee;
        // The last month of a term clears what is left rather than paying an
        // annuity factor over no months at all. Rounding can leave an öre or
        // two behind, which is cleared here rather than carried as a debt.
        const bool finalMonth = loan.terms.termMonths > 0 && loan.monthsRemaining <= 1;
        const Money payment = finalMonth ? loan.balance + cost : loan.paymentInForce;
        if (payment <= cost) {
            year.loanNeverRepaid = true;
            return;
        }

        const Money principal = payment - cost;
        loan.balance = loan.balance <= principal ? Money::zero(loan.balance.currency())
                                                 : loan.balance - principal;
        if (loan.monthsRemaining > 0)
            --loan.monthsRemaining;
        return;
    }

    // No payment recorded is zero here, and a negative one is broken input:
    // neither may turn the loan into a growing balance.
    if (!loan.terms.regularPayment.isPositive())
        return;

    loan.balance = loan.balance <= loan.terms.regularPayment
                       ? Money::zero(loan.balance.currency())
                       : loan.balance - loan.terms.regularPayment;
}

/// The monthly payments the household is charged this year, added up. A loan
/// that is already repaid, or that states no payment, is charged nothing.
Money totalPayment(const std::vector<LoanState>& loans, Currency currency)
{
    Money total = Money::zero(currency);
    for (const LoanState& loan : loans) {
        if (loan.balance.isPositive() && loan.paymentInForce.isPositive())
            total += loan.paymentInForce;
    }
    return total;
}

Money totalBalance(const std::vector<LoanState>& loans, Currency currency)
{
    Money total = Money::zero(currency);
    for (const LoanState& loan : loans)
        total += loan.balance;
    return total;
}

/// The unit every debt figure is reported in. A loan states its own currency,
/// and a household with none borrows today's unit from its starting balances:
/// zero owed is still zero of something.
Currency debtCurrencyOf(const ProjectionInput& input)
{
    if (!input.loans.empty())
        return input.loans.front().balance.currency();
    return input.startingBalances.front().currency();
}

/// Fills the derived figures of a row: the total, the debt and the two ratios
/// the model headlines. Called for every row, so today is measured the same way
/// as any projected year. Net worth counts both asset families, because the
/// debt it subtracts is secured on both: a mortgage sits on a house the total
/// never held. The ratios deliberately still read the total alone, because a
/// mortgage does not make investable capital last longer.
void fillDerived(ProjectionYear& year, const ProjectionInput& input)
{
    year.total = sumOf(year.perBucket);
    year.netWorth = year.total + year.otherAssets - year.debt;
    year.yearsOfFreedom = ratioOf(year.total, input.annualExpenses);
    year.fiProgress = ratioOf(year.total, input.fiTarget);
}

} // namespace

Money annuityPayment(const Money& balance, Percentage annualRate, int months,
                     const Money& monthlyFee)
{
    if (!balance.isPositive() || months <= 0)
        return Money::zero(balance.currency());

    // Never negative: a rate that is negative is broken input rather than a
    // rebate, and a loan with no interest simply divides the balance.
    const double rate = annualRate.isNegative() ? 0.0 : monthlyRateOf(annualRate);
    const double principal = static_cast<double>(balance.minorUnits());
    double payment = 0.0;
    if (rate == 0.0) {
        payment = principal / static_cast<double>(months);
    } else {
        // The one place the domain computes in floating point: (1 + r)^-n has
        // no exact integer form. The result is rounded to minor units on the
        // next line and never stored as a double, so the balance stays exact
        // and only the payment carries a sub-öre decision.
        const double factor = rate / (1.0 - std::pow(1.0 + rate, -months));
        payment = principal * factor;
    }

    const Money amortisation{static_cast<std::int64_t>(std::llround(payment)), balance.currency()};
    // A fee is a cost, never principal, and a negative one is broken input.
    if (monthlyFee.isNegative())
        return amortisation;
    return amortisation + monthlyFee;
}

std::vector<ProjectionYear> project(const ProjectionInput& input)
{
    // A negative horizon is a caller bug, not a household state. Treating it as
    // "today only" keeps every caller safe without inventing an exception for
    // what is otherwise a pure function.
    const int steps = input.years < 0 ? 0 : input.years;
    const Currency currency = debtCurrencyOf(input);

    std::vector<ProjectionYear> years;
    years.reserve(static_cast<std::size_t>(steps) + 1);
    std::vector<LoanState> loans = statesOf(input.loans);
    renewPayments(loans);

    Money interestPaid = Money::zero(currency);
    Money feesPaid = Money::zero(currency);

    ProjectionYear today;
    today.year = 0;
    today.perBucket = input.startingBalances;
    today.debt = totalBalance(loans, currency);
    today.interestPaid = interestPaid;
    today.feesPaid = feesPaid;
    today.loanPayment = totalPayment(loans, currency);
    today.otherAssets = input.startingOtherAssets;
    fillDerived(today, input);
    years.push_back(today);

    for (int step = 1; step <= steps; ++step) {
        ProjectionYear year;
        year.year = step;

        for (const Bucket bucket : allBuckets()) {
            const std::size_t index = static_cast<std::size_t>(bucket);
            const Money previous = years.back().perBucket[index];
            const Money growth = previous.scaledBy(input.expectedRealReturn[index]);
            year.perBucket[index] = previous + growth + input.annualContributions[index];
        }

        // January: a loan whose payment follows from its term is recalculated
        // from the balance left and the months left, and the payment is then
        // held for the whole year - the part of CSN's behaviour that a payment
        // fixed for the whole term gets wrong, because it pays the loan off
        // early and absorbs a rate change in the wrong place.
        renewPayments(loans);
        year.loanPayment = totalPayment(loans, currency);

        // A year of repayment is twelve repayments, because an annuity's split
        // between interest and principal changes every month as its balance
        // falls. The cumulative costs grow with it.
        YearRepayment repayment{Money::zero(currency), Money::zero(currency), false};
        for (int month = 0; month < monthsPerYear; ++month) {
            for (LoanState& loan : loans)
                repayOneMonth(loan, repayment);
        }
        interestPaid += repayment.interestPaid;
        feesPaid += repayment.feesPaid;

        year.debt = totalBalance(loans, currency);
        year.interestPaid = interestPaid;
        year.feesPaid = feesPaid;
        year.loanNeverRepaid = repayment.loanNeverRepaid;
        // Nothing is added to the other assets: the saving the household
        // records is already all in the buckets or in the amortisation, and
        // counting it here as well would grow the same krona twice.
        const Money otherSoFar = years.back().otherAssets;
        year.otherAssets = otherSoFar + otherSoFar.scaledBy(input.otherAssetsRealReturn);
        fillDerived(year, input);
        years.push_back(year);
    }

    return years;
}

std::optional<int> yearsToTarget(const ProjectionInput& input)
{
    // Scanning the projection instead of growing a second time keeps the two
    // answers in step: there is one place where a year is computed, so the
    // year count can never disagree with the chart beside it.
    for (const ProjectionYear& year : project(input)) {
        if (!(year.total < input.fiTarget))
            return year.year;
    }
    return std::nullopt;
}

} // namespace hinkwise::domain
