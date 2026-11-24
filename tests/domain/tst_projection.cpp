// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/bucket.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/projection.h"

#include <QTest>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using namespace hinkwise::domain;

namespace {

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

std::int64_t ore(Money amount)
{
    return amount.minorUnits();
}

std::int32_t bps(Percentage percentage)
{
    return percentage.basisPoints();
}

std::size_t indexOf(Bucket bucket)
{
    return static_cast<std::size_t>(bucket);
}

Money bucketBalance(const ProjectionYear& year, Bucket bucket)
{
    return year.perBucket[indexOf(bucket)];
}

ProjectionInput emptyInput()
{
    ProjectionInput input;
    input.years = 1;
    return input;
}

/// A loan repaid by a fixed principal amount each month.
LoanTerms fixedLoan(qint64 balance, int annualRateBp, qint64 monthlyPayment)
{
    return LoanTerms{sek(balance), Percentage::fromBasisPoints(annualRateBp),
                     RepaymentStyle::FixedAmortisation, sek(monthlyPayment), sek(0)};
}

/// A loan repaid by one fixed monthly sum covering interest, fee and principal.
LoanTerms annuityLoan(qint64 balance, int annualRateBp, qint64 monthlyPayment,
                      qint64 monthlyFee = 0)
{
    return LoanTerms{sek(balance), Percentage::fromBasisPoints(annualRateBp),
                     RepaymentStyle::Annuity, sek(monthlyPayment), sek(monthlyFee)};
}

} // namespace

class TestProjection : public QObject {
    Q_OBJECT

private slots:
    void yearZeroIsToday();
    void oneYearOfFivePercentReturn();
    void contributionsAreAddedOncePerYear();
    void negativeRealReturnShrinksTheBucket_data();
    void negativeRealReturnShrinksTheBucket();
    void totalIsTheSumOfTheBuckets();
    void yearsOfFreedomAndFiProgress();
    void zeroDenominatorsYieldZero();
    void yearsToTargetWhenAlreadyFunded();
    void yearsToTargetFindsTheFirstYear();
    void yearsToTargetWhenNeverFunded();
    void yearsToTargetForAZeroTarget();
    void zeroYearsYieldsTodayOnly();
    void negativeHorizonYieldsTodayOnly();
    void aFixedAmortisationLoanFallsLinearlyAndStopsAtZero();
    void aLoanThatIsClearedStaysAtZero();
    void aZeroPaymentLeavesTheDebtFlat();
    void annuityPrincipalShareRisesAsTheBalanceFalls();
    void anAnnuityEndsAtTheStatedPayment();
    void anUnderServicingAnnuityKeepsTheBalanceFlatAndFlagsIt();
    void mixedLoansSumIndependently();
    void interestAccruesAndIsReported();
    void feesAccrueForAnAnnuity();
    void zeroRateLoansClearExactly();
    void netWorthIsTheTotalMinusDebt();
    void netWorthCountsOtherAssets();
    void noOtherAssetsChangesNothing();
    void otherAssetsCompoundAtTheGivenRealReturn();
    void amortisationReachesNoInvestableCapital();
    void debtDoesNotChangeYearsOfFreedom();
    void noDebtLeavesNetWorthEqualToTheTotal();
};

void TestProjection::yearZeroIsToday()
{
    ProjectionInput input = emptyInput();
    input.startingBalances = {sek(1000000), sek(2000000), sek(3000000), sek(4000000)};
    // A year of growth is configured but must not touch today's row.
    input.expectedRealReturn = {Percentage::fromBasisPoints(500), Percentage::fromBasisPoints(500),
                                Percentage::fromBasisPoints(500), Percentage::fromBasisPoints(500)};
    input.annualContributions = {sek(50000), sek(50000), sek(50000), sek(50000)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(years.size(), static_cast<std::size_t>(2));
    QCOMPARE(years.front().year, 0);
    QCOMPARE(ore(bucketBalance(years.front(), Bucket::Buffer)), qint64(1000000));
    QCOMPARE(ore(bucketBalance(years.front(), Bucket::Preserve)), qint64(2000000));
    QCOMPARE(ore(bucketBalance(years.front(), Bucket::Growth)), qint64(3000000));
    QCOMPARE(ore(bucketBalance(years.front(), Bucket::Play)), qint64(4000000));
    QCOMPARE(ore(years.front().total), qint64(10000000));
}

void TestProjection::oneYearOfFivePercentReturn()
{
    ProjectionInput input = emptyInput();
    input.startingBalances[indexOf(Bucket::Growth)] = sek(10000000); // 100 000 kr
    input.expectedRealReturn[indexOf(Bucket::Growth)] = Percentage::fromBasisPoints(500);

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(years.size(), static_cast<std::size_t>(2));
    QCOMPARE(years[1].year, 1);
    QCOMPARE(ore(bucketBalance(years[1], Bucket::Growth)), qint64(10500000)); // 105 000 kr
    // The bucket that was left alone stays where it was.
    QCOMPARE(ore(bucketBalance(years[1], Bucket::Buffer)), qint64(0));
}

void TestProjection::contributionsAreAddedOncePerYear()
{
    ProjectionInput input = emptyInput();
    input.years = 3;
    input.annualContributions[indexOf(Bucket::Growth)] = sek(1200000); // 12 000 kr

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(years.size(), static_cast<std::size_t>(4));
    QCOMPARE(ore(bucketBalance(years[1], Bucket::Growth)), qint64(1200000));
    QCOMPARE(ore(bucketBalance(years[2], Bucket::Growth)), qint64(2400000));
    QCOMPARE(ore(bucketBalance(years[3], Bucket::Growth)), qint64(3600000));

    // The contribution arrives after that year's growth, so year one is exactly
    // the contribution, and only year two earns anything on it:
    // 10 000 kr grows by 5 % to 10 500 kr and another 10 000 kr is added.
    ProjectionInput earning = emptyInput();
    earning.years = 2;
    earning.annualContributions[indexOf(Bucket::Growth)] = sek(1000000); // 10 000 kr
    earning.expectedRealReturn[indexOf(Bucket::Growth)] = Percentage::fromBasisPoints(500);

    const std::vector<ProjectionYear> earned = project(earning);
    QCOMPARE(ore(bucketBalance(earned[1], Bucket::Growth)), qint64(1000000));
    QCOMPARE(ore(bucketBalance(earned[2], Bucket::Growth)), qint64(2050000));
}

void TestProjection::negativeRealReturnShrinksTheBucket_data()
{
    QTest::addColumn<int>("returnBasisPoints");
    QTest::addColumn<qint64>("expected");

    // The buffer's real band reaches -1 %, so a shrinking balance is a state the
    // projection has to model rather than a mistake to reject.
    QTest::newRow("minus one percent") << -100 << qint64(9900000);
    QTest::newRow("minus five percent") << -500 << qint64(9500000);
}

void TestProjection::negativeRealReturnShrinksTheBucket()
{
    QFETCH(int, returnBasisPoints);
    QFETCH(qint64, expected);

    ProjectionInput input = emptyInput();
    input.startingBalances[indexOf(Bucket::Buffer)] = sek(10000000); // 100 000 kr
    input.expectedRealReturn[indexOf(Bucket::Buffer)] =
        Percentage::fromBasisPoints(returnBasisPoints);

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(bucketBalance(years[1], Bucket::Buffer)), expected);
    QVERIFY(bucketBalance(years[1], Bucket::Buffer).minorUnits() <
            ore(bucketBalance(years.front(), Bucket::Buffer)));
}

void TestProjection::totalIsTheSumOfTheBuckets()
{
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.startingBalances = {sek(500000), sek(2000000), sek(9000000), sek(300000)};
    input.expectedRealReturn = {Percentage::fromBasisPoints(-100), Percentage::fromBasisPoints(100),
                                Percentage::fromBasisPoints(700), Percentage::fromBasisPoints(400)};
    input.annualContributions = {sek(0), sek(0), sek(2400000), sek(120000)};

    const std::vector<ProjectionYear> years = project(input);

    for (const ProjectionYear& year : years) {
        qint64 sum = 0;
        for (const Bucket bucket : allBuckets())
            sum += ore(bucketBalance(year, bucket));
        QCOMPARE(ore(year.total), qint64(sum));
    }

    // Growth alone: 90 000 kr earning 7 % and taking 24 000 kr a year, which is
    // 1 203 000 kr after one year and 1 527 210 kr after two.
    QCOMPARE(ore(bucketBalance(years[2], Bucket::Growth)), qint64(15272100));
}

void TestProjection::yearsOfFreedomAndFiProgress()
{
    ProjectionInput input = emptyInput();
    input.years = 0;
    // 500 000 kr in the buckets, 250 000 kr a year of expenses, a 1 000 000 kr
    // target: two years of freedom and half the target.
    input.startingBalances[indexOf(Bucket::Growth)] = sek(50000000);
    input.annualExpenses = sek(25000000);
    input.fiTarget = sek(100000000);

    const ProjectionYear today = project(input).front();

    QCOMPARE(bps(today.yearsOfFreedom), 20000);
    QCOMPARE(bps(today.fiProgress), 5000);

    // One year of freedom is 10 000 bp, and the ratio rounds half away from zero
    // rather than truncating: 100 000 / 300 000 is 3333.3 bp.
    ProjectionInput third = emptyInput();
    third.years = 0;
    third.startingBalances[indexOf(Bucket::Buffer)] = sek(10000000);
    third.annualExpenses = sek(30000000);

    QCOMPARE(bps(project(third).front().yearsOfFreedom), 3333);

    ProjectionInput half = emptyInput();
    half.years = 0;
    half.startingBalances[indexOf(Bucket::Buffer)] = sek(10001000);
    half.annualExpenses = sek(20000000);

    QCOMPARE(bps(project(half).front().yearsOfFreedom), 5001);
}

void TestProjection::zeroDenominatorsYieldZero()
{
    ProjectionInput input = emptyInput();
    input.startingBalances[indexOf(Bucket::Growth)] = sek(50000000);
    // No expenses and no target recorded yet: the arithmetic still answers.
    input.annualExpenses = sek(0);
    input.fiTarget = sek(0);

    const ProjectionYear today = project(input).front();

    QCOMPARE(bps(today.yearsOfFreedom), 0);
    QCOMPARE(bps(today.fiProgress), 0);

    // A negative denominator has no more meaning than a zero one, so it answers
    // the same way instead of turning a broken figure into a huge one.
    ProjectionInput broken = emptyInput();
    broken.startingBalances[indexOf(Bucket::Growth)] = sek(50000000);
    broken.annualExpenses = sek(-1000000);

    QCOMPARE(bps(project(broken).front().yearsOfFreedom), 0);
}

void TestProjection::yearsToTargetWhenAlreadyFunded()
{
    ProjectionInput input = emptyInput();
    input.years = 30;
    input.startingBalances[indexOf(Bucket::Growth)] = sek(200000000); // 2 000 000 kr
    input.fiTarget = sek(100000000);                                  // 1 000 000 kr

    const std::optional<int> reached = yearsToTarget(input);

    QVERIFY(reached.has_value());
    QCOMPARE(*reached, 0);
}

void TestProjection::yearsToTargetFindsTheFirstYear()
{
    ProjectionInput input = emptyInput();
    input.years = 10;
    input.annualContributions[indexOf(Bucket::Growth)] = sek(10000000); // 100 000 kr
    input.fiTarget = sek(25000000);                                     // 250 000 kr

    const std::optional<int> reached = yearsToTarget(input);

    QVERIFY(reached.has_value());
    // 0, 100 000, 200 000, 300 000: the third year is the first at or above.
    QCOMPARE(*reached, 3);
}

void TestProjection::yearsToTargetWhenNeverFunded()
{
    ProjectionInput input = emptyInput();
    input.years = 5;
    input.annualContributions[indexOf(Bucket::Growth)] = sek(1000000); // 10 000 kr
    input.fiTarget = sek(100000000);                                   // 1 000 000 kr

    QVERIFY(!yearsToTarget(input).has_value());

    // The horizon is the whole answer: the same household funded by year six is
    // never funded within a five-year horizon.
    ProjectionInput shorter = input;
    shorter.years = 4;
    QVERIFY(!yearsToTarget(shorter).has_value());

    ProjectionInput longer = input;
    longer.years = 100;
    QCOMPARE(*yearsToTarget(longer), 100);
}

void TestProjection::yearsToTargetForAZeroTarget()
{
    // A zero target costs nothing to reach, so it must not be read as "no target
    // at all" and answered with nullopt.
    ProjectionInput input = emptyInput();
    input.fiTarget = sek(0);

    const std::optional<int> reached = yearsToTarget(input);

    QVERIFY(reached.has_value());
    QCOMPARE(*reached, 0);

    // The same answer when the household has money of its own.
    ProjectionInput funded = input;
    funded.startingBalances[indexOf(Bucket::Growth)] = sek(50000000);
    QCOMPARE(*yearsToTarget(funded), 0);
}

void TestProjection::zeroYearsYieldsTodayOnly()
{
    ProjectionInput input = emptyInput();
    input.years = 0;
    input.startingBalances[indexOf(Bucket::Play)] = sek(700000);

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(years.size(), static_cast<std::size_t>(1));
    QCOMPARE(years.front().year, 0);
    QCOMPARE(ore(bucketBalance(years.front(), Bucket::Play)), qint64(700000));
}

void TestProjection::negativeHorizonYieldsTodayOnly()
{
    ProjectionInput input = emptyInput();
    input.years = -5;

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(years.size(), static_cast<std::size_t>(1));
    QCOMPARE(years.front().year, 0);
}

void TestProjection::aFixedAmortisationLoanFallsLinearlyAndStopsAtZero()
{
    ProjectionInput input = emptyInput();
    input.years = 5;
    // 50 000 kr at zero rate repaid at 1 000 kr a month: 12 000 kr a year,
    // which is the same amount every year until the loan is gone.
    input.loans = {fixedLoan(5000000, 0, 100000)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[0].debt), qint64(5000000));
    QCOMPARE(ore(years[1].debt), qint64(3800000));
    QCOMPARE(ore(years[2].debt), qint64(2600000));
    QCOMPARE(ore(years[3].debt), qint64(1400000));
    QCOMPARE(ore(years[4].debt), qint64(200000));
    // The last payment clears the loan; it does not lend the household money.
    QCOMPARE(ore(years[5].debt), qint64(0));

    // A negative balance is not a debt the model can reason about either, so
    // today is floored the same way.
    ProjectionInput negative = emptyInput();
    negative.loans = {fixedLoan(-1000000, 0, 100000)};
    QCOMPARE(ore(project(negative).front().debt), qint64(0));
}

void TestProjection::aLoanThatIsClearedStaysAtZero()
{
    ProjectionInput input = emptyInput();
    input.years = 5;
    input.loans = {fixedLoan(3000000, 0, 100000)}; // 30 000 kr, 1 000 kr a month

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[2].debt), qint64(600000));
    for (std::size_t year = 3; year < years.size(); ++year)
        QCOMPARE(ore(years[year].debt), qint64(0));
}

void TestProjection::aZeroPaymentLeavesTheDebtFlat()
{
    // A loan with no terms recorded comes out of the upgrade as fixed
    // amortisation with a zero payment, and a negative payment is broken input:
    // neither may turn the loan into an invented paydown or a growing balance.
    for (const qint64 payment : {qint64(0), qint64(-1000000)}) {
        ProjectionInput input = emptyInput();
        input.years = 3;
        input.loans = {fixedLoan(5000000, 0, payment)};

        const std::vector<ProjectionYear> years = project(input);

        for (const ProjectionYear& year : years)
            QCOMPARE(ore(year.debt), qint64(5000000));
    }

    // No loans at all is a zero debt, not a paydown of one.
    const std::vector<ProjectionYear> none = project(emptyInput());
    QCOMPARE(ore(none.front().debt), qint64(0));
}

void TestProjection::annuityPrincipalShareRisesAsTheBalanceFalls()
{
    // 100 000 kr at 12 % repaid as one fixed monthly sum of 1 500 kr: early
    // payments are mostly interest, and as the balance falls the interest share
    // falls with it. A yearly approximation would split every year the same.
    ProjectionInput input = emptyInput();
    input.years = 4;
    input.loans = {annuityLoan(10000000, 1200, 150000)};

    const std::vector<ProjectionYear> years = project(input);

    const Money firstYearInterest = years[1].interestPaid - years[0].interestPaid;
    const Money secondYearInterest = years[2].interestPaid - years[1].interestPaid;
    QVERIFY(secondYearInterest < firstYearInterest);

    const Money firstYearPrincipal = years[0].debt - years[1].debt;
    const Money secondYearPrincipal = years[1].debt - years[2].debt;
    QVERIFY(firstYearPrincipal < secondYearPrincipal);
    QVERIFY(years[2].debt.isPositive());
}

void TestProjection::anAnnuityEndsAtTheStatedPayment()
{
    // 1 000 kr at 12 % repaid as 100 kr a month. The payment never changes; the
    // loan ends when the balance does, eleven months in.
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.loans = {annuityLoan(100000, 1200, 10000)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[0].debt), qint64(100000));
    QCOMPARE(ore(years[1].debt), qint64(0));
    QCOMPARE(ore(years[1].interestPaid), qint64(5898));
    QCOMPARE(ore(years[1].feesPaid), qint64(0));
    QVERIFY(!years[1].loanNeverRepaid);
    // Once the loan is gone it stays gone and costs nothing more.
    QCOMPARE(ore(years[2].debt), qint64(0));
    QCOMPARE(ore(years[2].interestPaid), qint64(5898));
}

void TestProjection::anUnderServicingAnnuityKeepsTheBalanceFlatAndFlagsIt()
{
    // 100 000 kr at 12 % costs 1 000 kr a month in interest alone. A 1 000 kr
    // payment with a 100 kr fee does not even cover that, so no principal can be
    // paid: the balance stays where it is and the year says why.
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.loans = {annuityLoan(10000000, 1200, 100000, 10000)};

    const std::vector<ProjectionYear> years = project(input);

    QVERIFY(!years.front().loanNeverRepaid);
    for (std::size_t year = 1; year < years.size(); ++year) {
        QCOMPARE(ore(years[year].debt), qint64(10000000));
        QVERIFY(years[year].loanNeverRepaid);
    }
    QCOMPARE(ore(years[1].interestPaid), qint64(1200000));
    QCOMPARE(ore(years[1].feesPaid), qint64(120000));
    QCOMPARE(ore(years[2].interestPaid), qint64(2400000));
    QCOMPARE(ore(years[2].feesPaid), qint64(240000));
}

void TestProjection::mixedLoansSumIndependently()
{
    // A 2 000 000 kr mortgage at 2.65 % repaid with 3 000 kr of principal a
    // month beside a 300 000 kr student loan at 1.5 % repaid as a 1 500 kr
    // annuity with a 50 kr fee. Each loan has its own terms, and the household's
    // figures are the two of them added up - not one blended rate.
    ProjectionInput mortgage = emptyInput();
    mortgage.years = 3;
    mortgage.loans = {fixedLoan(200000000, 265, 300000)};

    ProjectionInput student = emptyInput();
    student.years = 3;
    student.loans = {annuityLoan(30000000, 150, 150000, 5000)};

    ProjectionInput mixed = emptyInput();
    mixed.years = 3;
    mixed.loans = {mortgage.loans.front(), student.loans.front()};

    const std::vector<ProjectionYear> mortgageYears = project(mortgage);
    const std::vector<ProjectionYear> studentYears = project(student);
    const std::vector<ProjectionYear> mixedYears = project(mixed);

    for (std::size_t year = 0; year < mixedYears.size(); ++year) {
        QCOMPARE(ore(mixedYears[year].debt),
                 ore(mortgageYears[year].debt) + ore(studentYears[year].debt));
        QCOMPARE(ore(mixedYears[year].interestPaid),
                 ore(mortgageYears[year].interestPaid) + ore(studentYears[year].interestPaid));
        QCOMPARE(ore(mixedYears[year].feesPaid),
                 ore(mortgageYears[year].feesPaid) + ore(studentYears[year].feesPaid));
    }

    // Both balances fall, and the student loan's fee is a cost of its own.
    QVERIFY(mixedYears.back().debt < mixedYears.front().debt);
    QCOMPARE(ore(mixedYears[1].feesPaid), qint64(60000));
    QVERIFY(mixedYears[1].interestPaid.isPositive());
}

void TestProjection::interestAccruesAndIsReported()
{
    // 12 000 kr at 12 % costs 120 kr a month, 1 440 kr a year, even when no
    // principal is being paid: the debt is a cost to hold as well as a balance.
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.loans = {fixedLoan(1200000, 1200, 0)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[0].interestPaid), qint64(0));
    QCOMPARE(ore(years[1].interestPaid), qint64(144000));
    QCOMPARE(ore(years[2].interestPaid), qint64(288000));
    QCOMPARE(ore(years[1].feesPaid), qint64(0));
    for (const ProjectionYear& year : years)
        QCOMPARE(ore(year.debt), qint64(1200000));
}

void TestProjection::feesAccrueForAnAnnuity()
{
    ProjectionInput input = emptyInput();
    input.years = 3;
    input.loans = {annuityLoan(5000000, 1200, 100000, 5000)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[1].feesPaid), qint64(60000));
    QCOMPARE(ore(years[3].feesPaid), qint64(180000));
    // The fee is a cost, never principal: the balance still falls by the
    // payment less the interest and the fee.
    QVERIFY(years[3].debt < years[0].debt);
}

void TestProjection::zeroRateLoansClearExactly()
{
    // With no interest there is nothing to split: the monthly payment is
    // principal and twelve of them clear the loan exactly.
    for (const LoanTerms& loan : {fixedLoan(1200000, 0, 100000), annuityLoan(1200000, 0, 100000)}) {
        ProjectionInput input = emptyInput();
        input.years = 1;
        input.loans = {loan};

        const std::vector<ProjectionYear> years = project(input);

        QCOMPARE(ore(years[1].debt), qint64(0));
        QCOMPARE(ore(years[1].interestPaid), qint64(0));
        QCOMPARE(ore(years[1].feesPaid), qint64(0));
        QVERIFY(!years[1].loanNeverRepaid);
    }
}

void TestProjection::netWorthIsTheTotalMinusDebt()
{
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.startingBalances[indexOf(Bucket::Growth)] = sek(10000000); // 100 000 kr
    input.annualContributions[indexOf(Bucket::Growth)] = sek(1000000);
    input.loans = {fixedLoan(4000000, 0, 50000)}; // 40 000 kr, 500 kr a month

    const std::vector<ProjectionYear> years = project(input);

    for (const ProjectionYear& year : years)
        QCOMPARE(ore(year.netWorth), ore(year.total) - ore(year.debt));

    QCOMPARE(ore(years.front().netWorth), qint64(6000000));
    // 100 000 + 10 000 twice, less the 28 000 still owed in year two.
    QCOMPARE(ore(years[2].total), qint64(12000000));
    QCOMPARE(ore(years[2].debt), qint64(2800000));
    QCOMPARE(ore(years[2].netWorth), qint64(9200000));
}

void TestProjection::netWorthCountsOtherAssets()
{
    // A household with 100 000 kr of capital, the 2 000 000 kr flat the loan is
    // secured on and a 1 500 000 kr mortgage. Net worth has to count the flat,
    // or the mortgage alone would make the household look poorer than it is.
    ProjectionInput input = emptyInput();
    input.years = 5;
    input.startingBalances[indexOf(Bucket::Growth)] = sek(10000000);
    input.startingOtherAssets = sek(200000000);
    input.otherAssetsRealReturn = Percentage::fromBasisPoints(250);
    input.loans = {fixedLoan(150000000, 0, 100000)};

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years.front().otherAssets), qint64(200000000));
    for (const ProjectionYear& year : years)
        QCOMPARE(ore(year.netWorth), ore(year.total) + ore(year.otherAssets) - ore(year.debt));

    // Today: 100 000 kr of capital plus 2 000 000 kr of housing less the loan.
    QCOMPARE(ore(years.front().netWorth), qint64(60000000));
    // The total stays investable capital: a flat is not spendable, so it never
    // joins the buckets and never moves the ratios.
    QCOMPARE(ore(years.back().total), qint64(10000000));
}

void TestProjection::noOtherAssetsChangesNothing()
{
    // A household that owns no housing and has no pension promises: the second
    // asset family is empty, so net worth is what it always was, and a return
    // set for it multiplies nothing.
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.startingBalances[indexOf(Bucket::Growth)] = sek(10000000);
    input.annualContributions[indexOf(Bucket::Growth)] = sek(1000000);
    input.otherAssetsRealReturn = Percentage::fromBasisPoints(250);
    input.loans = {fixedLoan(4000000, 0, 50000)};

    const std::vector<ProjectionYear> years = project(input);

    for (const ProjectionYear& year : years) {
        QCOMPARE(ore(year.otherAssets), qint64(0));
        QCOMPARE(ore(year.netWorth), ore(year.total) - ore(year.debt));
    }
}

void TestProjection::otherAssetsCompoundAtTheGivenRealReturn()
{
    // 100 000 kr at the middle of the preserve band, 2.5 % real: the same growth
    // the buckets get, applied to the family that is not in them.
    ProjectionInput input = emptyInput();
    input.years = 3;
    input.startingOtherAssets = sek(10000000);
    input.otherAssetsRealReturn = Percentage::fromBasisPoints(250);

    const std::vector<ProjectionYear> years = project(input);

    QCOMPARE(ore(years[0].otherAssets), qint64(10000000));
    QCOMPARE(ore(years[1].otherAssets), qint64(10250000));
    QCOMPARE(ore(years[2].otherAssets), qint64(10506250));
    QCOMPARE(ore(years[3].otherAssets), qint64(10768906));

    // Nothing is ever added to it: a saving is capital or it is amortisation,
    // and neither arrives here.
    ProjectionInput saving = input;
    saving.annualContributions[indexOf(Bucket::Buffer)] = sek(1200000);
    const std::vector<ProjectionYear> withSaving = project(saving);
    for (std::size_t year = 0; year < years.size(); ++year)
        QCOMPARE(ore(withSaving[year].otherAssets), ore(years[year].otherAssets));
}

void TestProjection::amortisationReachesNoInvestableCapital()
{
    // A month whose whole saving goes to the loan: nothing reaches the buffer or
    // the investing buckets, and the debt is the only figure that moves. The
    // saving is not counted twice - once as a contribution and once as a
    // repayment - because amortisation turns a loan into housing equity, and
    // housing is not investable capital.
    ProjectionInput allToTheLoan = emptyInput();
    allToTheLoan.years = 3;
    allToTheLoan.startingBalances[indexOf(Bucket::Preserve)] = sek(10000000);
    allToTheLoan.loans = {fixedLoan(5000000, 0, 100000)}; // 50 000 kr, 1 000 kr a month

    const std::vector<ProjectionYear> amortised = project(allToTheLoan);

    for (const ProjectionYear& year : amortised)
        QCOMPARE(ore(year.total), qint64(10000000));
    QCOMPARE(ore(amortised[1].debt), qint64(3800000));
    QCOMPARE(ore(amortised[3].debt), qint64(1400000));

    // Half the month to the buffer and half to the loan: 1 000 kr a month is
    // 12 000 kr a year of capital and 12 000 kr a year of repayment, and the two
    // move on their own sides of the household's position.
    ProjectionInput halfToTheBuffer = allToTheLoan;
    halfToTheBuffer.annualContributions[indexOf(Bucket::Buffer)] = sek(1200000);

    const std::vector<ProjectionYear> split = project(halfToTheBuffer);

    QCOMPARE(ore(split[1].total), qint64(11200000));
    QCOMPARE(ore(split[3].total), qint64(13600000));
    QCOMPARE(ore(split[1].debt), qint64(3800000));
    QCOMPARE(ore(split[3].debt), qint64(1400000));
    // Capital and debt part company by the same amount every year, so the
    // household is never counted as paying the loan with money it still has.
    QCOMPARE(ore(split[3].netWorth) - ore(split[0].netWorth), qint64(3600000) + qint64(3600000));
}

void TestProjection::debtDoesNotChangeYearsOfFreedom()
{
    ProjectionInput borrowed = emptyInput();
    borrowed.years = 2;
    borrowed.startingBalances[indexOf(Bucket::Growth)] = sek(50000000); // 500 000 kr
    borrowed.annualExpenses = sek(25000000);
    borrowed.fiTarget = sek(100000000);
    borrowed.loans = {fixedLoan(180000000, 0, 1000000)}; // a 1 800 000 kr mortgage

    ProjectionInput unencumbered = borrowed;
    unencumbered.loans.clear();

    const std::vector<ProjectionYear> withDebt = project(borrowed);
    const std::vector<ProjectionYear> withoutDebt = project(unencumbered);

    QCOMPARE(withDebt.size(), withoutDebt.size());
    for (std::size_t year = 0; year < withDebt.size(); ++year) {
        QCOMPARE(bps(withDebt[year].yearsOfFreedom), bps(withoutDebt[year].yearsOfFreedom));
        QCOMPARE(bps(withDebt[year].fiProgress), bps(withoutDebt[year].fiProgress));
    }
    // The same capital is worth more once the loan is smaller, but freedom is
    // measured on the capital: a mortgage does not make it last longer.
    QVERIFY(withDebt.back().netWorth < withoutDebt.back().netWorth);
}

void TestProjection::noDebtLeavesNetWorthEqualToTheTotal()
{
    ProjectionInput input = emptyInput();
    input.years = 2;
    input.startingBalances = {sek(500000), sek(2000000), sek(9000000), sek(300000)};
    input.expectedRealReturn = {Percentage::fromBasisPoints(-100), Percentage::fromBasisPoints(100),
                                Percentage::fromBasisPoints(700), Percentage::fromBasisPoints(400)};
    input.annualContributions = {sek(0), sek(0), sek(2400000), sek(120000)};

    const std::vector<ProjectionYear> years = project(input);

    for (const ProjectionYear& year : years) {
        QCOMPARE(ore(year.debt), qint64(0));
        QCOMPARE(ore(year.netWorth), ore(year.total));
    }
}

QTEST_APPLESS_MAIN(TestProjection)

#include "tst_projection.moc"
