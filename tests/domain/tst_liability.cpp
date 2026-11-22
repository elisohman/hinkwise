// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/liability.h"

#include <QTest>
#include <stdexcept>

using namespace hinkwise::domain;

namespace {

/// A typical Swedish mortgage rate used as the benchmark in these tests.
Percentage fourAndAHalfPercent()
{
    return Percentage::fromBasisPoints(450);
}

Liability creditCard()
{
    return makeLiability("card", "Credit card", LiabilityKind::CreditCard,
                         Percentage::fromBasisPoints(1995));
}

} // namespace

class TestLiability : public QObject {
    Q_OBJECT

private slots:
    void makeLiabilityKeepsItsFields();
    void makeLiabilityRejectsEmptyId();
    void makeLiabilityRejectsEmptyName();
    void makeLiabilityRejectsNegativeRate();
    void zeroRateIsAllowed();
    void isBadLoanAtTheBoundary();
    void isBadLoanComparesStrictly();
};

void TestLiability::makeLiabilityKeepsItsFields()
{
    const Liability loan = makeLiability("studielan", "Student loan", LiabilityKind::StudentLoan,
                                         Percentage::fromBasisPoints(150));

    QCOMPARE(QString::fromStdString(loan.id), QStringLiteral("studielan"));
    QCOMPARE(QString::fromStdString(loan.name), QStringLiteral("Student loan"));
    QCOMPARE(loan.kind, LiabilityKind::StudentLoan);
    QCOMPARE(loan.interestRate.basisPoints(), 150);
}

void TestLiability::makeLiabilityRejectsEmptyId()
{
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        makeLiability(std::string{}, "Mortgage", LiabilityKind::Mortgage, fourAndAHalfPercent()));
}

void TestLiability::makeLiabilityRejectsEmptyName()
{
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        makeLiability("bolan", std::string{}, LiabilityKind::Mortgage, fourAndAHalfPercent()));
}

void TestLiability::makeLiabilityRejectsNegativeRate()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                           Percentage::fromBasisPoints(-1)));
}

void TestLiability::zeroRateIsAllowed()
{
    const Liability loan = makeLiability("csn", "CSN", LiabilityKind::StudentLoan, Percentage{});
    QVERIFY(!isBadLoan(loan, fourAndAHalfPercent()));
}

void TestLiability::isBadLoanAtTheBoundary()
{
    // Equal to the benchmark is not bad: the model compares strictly.
    const Liability atBenchmark =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, fourAndAHalfPercent());
    QVERIFY(!isBadLoan(atBenchmark, fourAndAHalfPercent()));

    // One basis point above it is.
    const Liability justAbove =
        makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                      Percentage::fromBasisPoints(fourAndAHalfPercent().basisPoints() + 1));
    QVERIFY(isBadLoan(justAbove, fourAndAHalfPercent()));
}

void TestLiability::isBadLoanComparesStrictly()
{
    const Liability card = creditCard();
    QVERIFY(isBadLoan(card, fourAndAHalfPercent()));

    // The benchmark is a rate, not a list of kinds: a mortgage priced above it
    // is bad too, and a credit card below it is not.
    const Liability cheapCard = makeLiability("card", "Credit card", LiabilityKind::CreditCard,
                                              Percentage::fromBasisPoints(200));
    QVERIFY(!isBadLoan(cheapCard, fourAndAHalfPercent()));
}

QTEST_APPLESS_MAIN(TestLiability)

#include "tst_liability.moc"
