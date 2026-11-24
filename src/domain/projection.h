// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "liability.h"
#include "money.h"
#include "percentage.h"

#include <array>
#include <optional>
#include <vector>

namespace hinkwise::domain {

/// One loan's own repayment terms.
///
/// Loans are projected one by one rather than as one aggregate balance because
/// they do not carry the same rate: a krona paid on a 2.65 % mortgage and a
/// krona paid on a 1.5 % student loan are not the same krona. The split between
/// them follows from their own terms, so nothing has to be entered twice.
struct LoanTerms {
    /// What is left to repay today.
    Money balance{};
    Percentage annualRate{};
    RepaymentStyle style = RepaymentStyle::FixedAmortisation;
    /// MONTHLY. For FixedAmortisation this is the principal repaid; for Annuity
    /// it is the total paid, interest and fee included.
    Money regularPayment{};
    /// MONTHLY, Annuity only. A cost, never principal.
    Money monthlyFee{};
    /// Months the loan still has to run, zero when the loan states none. An
    /// annuity that states a term is not repaid on one payment fixed for its
    /// whole life: the term is what holds still and the payment is recalculated
    /// every year, which is what the term is here for.
    int termMonths = 0;
};

/// What a projection needs: the four buckets as they stand today, what is
/// added to them each year, the assets that are not in them, the two yardsticks
/// the model measures progress against, and the loans those families sit
/// beside.
///
/// Every return here is REAL. There is no inflation figure to supply and none
/// is applied, so a projection speaks in today's money throughout and the one
/// thing a household cannot predict stays out of the arithmetic.
///
/// The two asset families are projected separately on purpose. Investable
/// capital - the buckets - is what a household can live off, so it carries the
/// model's per-bucket returns and the yearly saving. Housing and pension
/// promises are wealth it cannot spend, so they are not in the buckets at all;
/// leaving them out of net worth too would report a mortgaged household as
/// poorer than it is, because the mortgage is secured on a house the buckets
/// never held. They therefore travel in `otherAssets`, at one conservative real
/// return, and only the sum of both families less the debt is net worth. The
/// yards-of-freedom and FI figures still read the buckets alone: a flat does
/// not make capital last longer.
struct ProjectionInput {
    /// Indexed by Bucket. Investable capital only: the household's holdings
    /// with housing and the four pension kinds left out, exactly the assets
    /// BalanceSheet::investablePerBucket sums. A flat or a pension promise is
    /// wealth, but a projection that counted it as spendable would report a
    /// household as funded on money it cannot live off.
    std::array<Money, 4> startingBalances{};
    /// Indexed by Bucket. Added once per year, after that year's growth.
    std::array<Money, 4> annualContributions{};
    /// Indexed by Bucket, per year. Negative values are legal and expected:
    /// the model's band for the buffer includes a -1 % real return.
    std::array<Percentage, 4> expectedRealReturn{};
    /// Everything the household owns that is NOT investable capital: housing
    /// and the four pension kinds, exactly the BalanceSheet::totalAssets that
    /// BalanceSheet::headlineAssets leaves out. Kept apart from the buckets
    /// because it is projected apart; the struct comment says why.
    Money startingOtherAssets{};
    /// Per year, real, for startingOtherAssets. Nothing is added to it: a paid
    /// loan turns into housing equity rather than into a contribution here.
    Percentage otherAssetsRealReturn{};
    /// For years of freedom.
    Money annualExpenses{};
    /// The FI target to measure progress against. May be zero, which means the
    /// household states no target and progress against it is zero.
    Money fiTarget{};
    /// Every loan the household carries, each with its own rate, style and
    /// monthly payment. Empty is a household that records no debt, which is a
    /// debt of zero rather than an invented paydown. A loan whose payment is
    /// zero is simulated without a paydown too: terms that were never recorded
    /// must not look like a loan being cleared. A loan that states a term is
    /// the exception - an annuity is then repaid on the payment its term and
    /// rate imply, recalculated once a year, and its recorded payment is only
    /// what the household last saw. See project().
    std::vector<LoanTerms> loans;
    /// Number of yearly steps. A negative value is treated as zero.
    int years = 30;
};

/// One row of the projection. Year 0 is today: the starting balances and the
/// starting debt, untouched.
struct ProjectionYear {
    /// 0 is today, 1 is one year from now.
    int year = 0;
    /// Indexed by Bucket.
    std::array<Money, 4> perBucket{};
    Money total{};
    /// The non-investable assets, grown at the real return they were given.
    /// Never part of total: the buckets and these grow on separate rates.
    Money otherAssets{};
    /// What is left to repay: every loan balance added up. Never below zero,
    /// because the last payment clears a loan rather than turning the household
    /// into a lender.
    Money debt{};
    /// Interest paid so far, cumulative over every loan. Interest is a cost and
    /// never principal: reporting it apart is what shows a household what the
    /// debt costs to hold, not only what it clears.
    Money interestPaid{};
    /// Administration fees paid so far, cumulative. Annuity loans only.
    Money feesPaid{};
    /// The monthly payment every loan is charged at during this year, added up:
    /// the recorded payment for a fixed amortisation loan (its principal part)
    /// and for an annuity with no term, the recalculated one for an annuity
    /// that states a term - interest, fee and principal together. A loan that is
    /// already cleared contributes nothing, so this is what the household would
    /// actually be asked for this year.
    Money loanPayment{};
    /// True in a year where an annuity's monthly payment does not cover its
    /// interest and fee, so its balance cannot fall. The balance is kept flat
    /// rather than grown forever, and this flag is how the interface can say the
    /// payment does not service the loan instead of drawing a debt that never
    /// moves without explanation.
    bool loanNeverRepaid = false;
    /// Everything owned less everything owed: total + otherAssets - debt.
    /// Neither the total nor the debt alone is net worth, because the debt is
    /// secured on what sits in otherAssets.
    Money netWorth{};
    /// Total over annual expenses; 10 000 bp == one year of freedom.
    Percentage yearsOfFreedom{};
    /// Total over fiTarget; 10 000 bp == fully funded.
    Percentage fiProgress{};
};

/// Yearly steps from today, inclusive of today: `years + 1` rows, never fewer
/// than one.
std::vector<ProjectionYear> project(const ProjectionInput& input);

/// The monthly payment CSN would charge on `balance` with `months` left to run
/// at `annualRate`, and zero when there is nothing to amortise or no month to
/// amortise it over.
///
/// The repayment part is the annuity factor: r / (1 - (1 + r)^-n) with r the
/// rate one month is charged at, which is what CSN recalculates each January
/// from what is left to repay and how long the loan still has to run. The
/// administration fee is added to it, so this is one amount covering interest,
/// fee and amortisation together - and the principal is then exactly the
/// annuity principal, which is what keeps the loan on its term instead of
/// letting the fee stretch it.
Money annuityPayment(const Money& balance, Percentage annualRate, int months,
                     const Money& monthlyFee);

/// The first year in which the total reaches fiTarget, or nullopt when it never
/// does within the horizon. Zero when the household is already funded today,
/// which is also the answer for a zero target: a target of nothing costs
/// nothing to reach.
std::optional<int> yearsToTarget(const ProjectionInput& input);

} // namespace hinkwise::domain
