// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/liability.h"

#include <QTest>
#include <cstdint>
#include <stdexcept>

using namespace hinkwise::domain;

namespace {

/// A typical Swedish mortgage rate used as the benchmark in these tests.
Percentage fourAndAHalfPercent()
{
    return Percentage::fromBasisPoints(450);
}

/// A loan rate stated in the basis points these tests read most easily. Ten
/// parts per 100 000 make one basis point, so the conversion is exact.
InterestRate rateOfBasisPoints(std::int32_t basisPoints)
{
    return InterestRate::fromPartsPer100k(basisPoints * 10);
}

Liability creditCard()
{
    return makeLiability("card", "Credit card", LiabilityKind::CreditCard, rateOfBasisPoints(1995));
}

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

} // namespace

class TestLiability : public QObject {
    Q_OBJECT

private slots:
    void makeLiabilityKeepsItsFields();
    void makeLiabilityRejectsEmptyId();
    void makeLiabilityRejectsEmptyName();
    void makeLiabilityRejectsNegativeRate();
    void makeLiabilityKeepsItsRepaymentTerms();
    void makeLiabilityRejectsANegativePayment();
    void makeLiabilityRejectsANegativeFee();
    void makeLiabilityRejectsAFeeOnFixedAmortisation();
    void makeLiabilityKeepsARemainingTerm();
    void makeLiabilityRejectsANegativeTerm();
    void aTermOnAFixedAmortisationLoanIsNotAnError();
    void zeroRateIsAllowed();
    void isBadLoanAtTheBoundary();
    void isBadLoanComparesStrictly();
    void keepsAThreeDecimalInterestRate();
    void convertsToPercentageAtBasisPointGranularity();
    void spreadsAnAnnualFeeOverTwelveMonths();
};

void TestLiability::makeLiabilityKeepsItsFields()
{
    const Liability loan = makeLiability("studielan", "Student loan", LiabilityKind::StudentLoan,
                                         rateOfBasisPoints(150));

    QCOMPARE(QString::fromStdString(loan.id), QStringLiteral("studielan"));
    QCOMPARE(QString::fromStdString(loan.name), QStringLiteral("Student loan"));
    QCOMPARE(loan.kind, LiabilityKind::StudentLoan);
    QCOMPARE(loan.interestRate.partsPer100k(), 1500);
}

void TestLiability::makeLiabilityRejectsEmptyId()
{
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        makeLiability(std::string{}, "Mortgage", LiabilityKind::Mortgage, rateOfBasisPoints(450)));
}

void TestLiability::makeLiabilityRejectsEmptyName()
{
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        makeLiability("bolan", std::string{}, LiabilityKind::Mortgage, rateOfBasisPoints(450)));
}

void TestLiability::makeLiabilityRejectsNegativeRate()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                           InterestRate::fromPartsPer100k(-10)));
}

void TestLiability::makeLiabilityKeepsItsRepaymentTerms()
{
    const Liability loan =
        makeLiability("csn", "Student loan", LiabilityKind::StudentLoan, rateOfBasisPoints(150),
                      RepaymentStyle::Annuity, sek(150000), sek(5000));

    QCOMPARE(static_cast<int>(loan.style), static_cast<int>(RepaymentStyle::Annuity));
    QCOMPARE(loan.regularPayment.minorUnits(), qint64(150000));
    QCOMPARE(loan.monthlyFee.minorUnits(), qint64(5000));

    // A loan with nothing recorded is fixed amortisation with a zero payment,
    // which is the model's "no terms yet" rather than a paid-off loan.
    const Liability untold =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, rateOfBasisPoints(450));
    QCOMPARE(static_cast<int>(untold.style), static_cast<int>(RepaymentStyle::FixedAmortisation));
    QVERIFY(untold.regularPayment.isZero());
    QVERIFY(untold.monthlyFee.isZero());
}

void TestLiability::makeLiabilityRejectsANegativePayment()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                           rateOfBasisPoints(450),
                                           RepaymentStyle::FixedAmortisation, sek(-1)));
}

void TestLiability::makeLiabilityRejectsANegativeFee()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeLiability("csn", "Student loan", LiabilityKind::StudentLoan,
                                           rateOfBasisPoints(150), RepaymentStyle::Annuity,
                                           sek(150000), sek(-1)));
}

void TestLiability::makeLiabilityRejectsAFeeOnFixedAmortisation()
{
    // The fee is part of what an annuity payment is split into; on a fixed
    // amortisation loan there is nothing for it to belong to.
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, rateOfBasisPoints(450),
                      RepaymentStyle::FixedAmortisation, sek(300000), sek(5000)));
}

void TestLiability::makeLiabilityKeepsARemainingTerm()
{
    // A student loan is described by the months it still has to run: CSN keeps
    // the term and recalculates what it asks for, so the term is the fact worth
    // recording.
    const Liability loan =
        makeLiability("csn", "Student loan", LiabilityKind::StudentLoan, rateOfBasisPoints(150),
                      RepaymentStyle::Annuity, sek(150000), sek(5000), 96);
    QCOMPARE(loan.termMonths, 96);

    // Not stated is the default state and not a term of no months.
    const Liability untold =
        makeLiability("csn", "Student loan", LiabilityKind::StudentLoan, rateOfBasisPoints(150),
                      RepaymentStyle::Annuity, sek(150000));
    QCOMPARE(untold.termMonths, 0);
}

void TestLiability::makeLiabilityRejectsANegativeTerm()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeLiability("csn", "Student loan", LiabilityKind::StudentLoan,
                                           rateOfBasisPoints(150), RepaymentStyle::Annuity,
                                           sek(150000), sek(0), -1));
}

void TestLiability::aTermOnAFixedAmortisationLoanIsNotAnError()
{
    // Changing a loan's style must not require clearing its term first, and the
    // term of a loan that repays a recorded principal means nothing: the model
    // ignores it rather than refusing the row.
    const Liability mortgage =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, rateOfBasisPoints(450),
                      RepaymentStyle::FixedAmortisation, sek(300000), sek(0), 240);
    QCOMPARE(mortgage.termMonths, 240);
    QCOMPARE(static_cast<int>(mortgage.style), static_cast<int>(RepaymentStyle::FixedAmortisation));
}

void TestLiability::zeroRateIsAllowed()
{
    const Liability loan = makeLiability("csn", "CSN", LiabilityKind::StudentLoan, InterestRate{});
    QVERIFY(!isBadLoan(loan, fourAndAHalfPercent()));
}

void TestLiability::isBadLoanAtTheBoundary()
{
    // Equal to the benchmark is not bad: the model compares strictly.
    const Liability atBenchmark =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, rateOfBasisPoints(450));
    QVERIFY(!isBadLoan(atBenchmark, fourAndAHalfPercent()));

    // One basis point above it is.
    const Liability justAbove =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                      rateOfBasisPoints(fourAndAHalfPercent().basisPoints() + 1));
    QVERIFY(isBadLoan(justAbove, fourAndAHalfPercent()));
}

void TestLiability::isBadLoanComparesStrictly()
{
    const Liability card = creditCard();
    QVERIFY(isBadLoan(card, fourAndAHalfPercent()));

    // The benchmark is a rate, not a list of kinds: a mortgage priced above it
    // is bad too, and a credit card below it is not.
    const Liability cheapCard =
        makeLiability("card", "Credit card", LiabilityKind::CreditCard, rateOfBasisPoints(200));
    QVERIFY(!isBadLoan(cheapCard, fourAndAHalfPercent()));
}

void TestLiability::keepsAThreeDecimalInterestRate()
{
    const Liability loan = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                         InterestRate::fromPercent(2.655),
                                         RepaymentStyle::FixedAmortisation, sek(300000));
    QCOMPARE(loan.interestRate.partsPer100k(), 2655);
    QCOMPARE(loan.interestRate.asPercent(), 2.655);

    // A rate is not rounded to the two decimals a Percentage holds.
    QCOMPARE(InterestRate::fromPercent(4.5678).partsPer100k(), 4568);
}

void TestLiability::convertsToPercentageAtBasisPointGranularity()
{
    // 4.504 % rounds down to 4.50 %, and 4.505 % rounds half away from zero to
    // 4.51 %. The benchmark comparison therefore happens at one basis point.
    QCOMPARE(InterestRate::fromPartsPer100k(4504).toPercentage().basisPoints(), 450);
    QCOMPARE(InterestRate::fromPartsPer100k(4505).toPercentage().basisPoints(), 451);
    QCOMPARE(InterestRate::fromPartsPer100k(4506).toPercentage().basisPoints(), 451);

    QVERIFY(!isBadLoan(makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                     InterestRate::fromPartsPer100k(4504)),
                       fourAndAHalfPercent()));
    QVERIFY(isBadLoan(makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                    InterestRate::fromPartsPer100k(4505)),
                      fourAndAHalfPercent()));
}

void TestLiability::spreadsAnAnnualFeeOverTwelveMonths()
{
    // 150 kr a year is 12.50 kr a month, and a whole-krona standing payment
    // rounds half away from zero to 13 kr.
    QCOMPARE(monthlyFeeFromAnnual(sek(15000)).minorUnits(), qint64(1300));
    QCOMPARE(monthlyFeeFromAnnual(sek(0)).minorUnits(), qint64(0));

    // A monthly charge that was already stored comes back unchanged: twelve of
    // it is the annual figure the field shows, and that figure spreads back.
    const Money stored = sek(5000);
    QCOMPARE(annualFeeFromMonthly(stored).minorUnits(), qint64(60000));
    QCOMPARE(monthlyFeeFromAnnual(annualFeeFromMonthly(stored)).minorUnits(), qint64(5000));
}

QTEST_APPLESS_MAIN(TestLiability)

#include "tst_liability.moc"
