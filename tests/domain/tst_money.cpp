// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/money.h"
#include "domain/percentage.h"

#include <QTest>
#include <stdexcept>

using namespace hinkwise::domain;

class TestMoney : public QObject {
    Q_OBJECT

private slots:
    void addsAndSubtracts_data();
    void addsAndSubtracts();
    void negates();
    void compares();
    void rejectsCurrencyMismatch();
    void scalesByPercentage_data();
    void scalesByPercentage();
    void scalesHalfAwayFromZero_data();
    void scalesHalfAwayFromZero();
    void splitsWithoutLosingMinorUnits_data();
    void splitsWithoutLosingMinorUnits();
    void formatsForLogs();
};

void TestMoney::addsAndSubtracts_data()
{
    QTest::addColumn<qint64>("left");
    QTest::addColumn<qint64>("right");
    QTest::addColumn<qint64>("sum");
    QTest::addColumn<qint64>("difference");

    QTest::newRow("zero") << qint64(0) << qint64(0) << qint64(0) << qint64(0);
    QTest::newRow("positive") << qint64(150050) << qint64(24950) << qint64(175000)
                              << qint64(125100);
    QTest::newRow("ending in debt")
        << qint64(1000) << qint64(2500) << qint64(3500) << qint64(-1500);
}

void TestMoney::addsAndSubtracts()
{
    QFETCH(qint64, left);
    QFETCH(qint64, right);
    QFETCH(qint64, sum);
    QFETCH(qint64, difference);

    const Money a{left, Currency::Sek};
    const Money b{right, Currency::Sek};

    QCOMPARE((a + b).minorUnits(), sum);
    QCOMPARE((a - b).minorUnits(), difference);
    QCOMPARE((a + b).currency(), Currency::Sek);
}

void TestMoney::negates()
{
    const Money m{1250, Currency::Eur};
    QCOMPARE((-m).minorUnits(), qint64(-1250));
    QCOMPARE((-(-m)).minorUnits(), qint64(1250));
}

void TestMoney::compares()
{
    const Money small{100, Currency::Usd};
    const Money large{200, Currency::Usd};

    QVERIFY(small < large);
    QVERIFY(small <= large);
    QVERIFY(large > small);
    QVERIFY(large >= small);

    // Braced initialisers cannot appear inside QVERIFY: its comma would be
    // read as an argument separator. Name the value instead.
    const Money sameAsSmall{100, Currency::Usd};
    QVERIFY(small == sameAsSmall);
    QVERIFY(small != large);
}

void TestMoney::rejectsCurrencyMismatch()
{
    const Money sek{100, Currency::Sek};
    const Money eur{100, Currency::Eur};

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sek + eur);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sek - eur);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sek < eur);
}

void TestMoney::scalesByPercentage_data()
{
    QTest::addColumn<qint64>("minorUnits");
    QTest::addColumn<int>("basisPoints");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("20% of 1000") << qint64(100000) << 2000 << qint64(20000);
    QTest::newRow("50% of odd") << qint64(101) << 5000 << qint64(51);
    QTest::newRow("zero percent") << qint64(12345) << 0 << qint64(0);
    QTest::newRow("100%") << qint64(12345) << 10000 << qint64(12345);
    QTest::newRow("real return 5%") << qint64(25000000) << 500 << qint64(1250000);
    QTest::newRow("negative amount") << qint64(-100000) << 2000 << qint64(-20000);
}

void TestMoney::scalesByPercentage()
{
    QFETCH(qint64, minorUnits);
    QFETCH(int, basisPoints);
    QFETCH(qint64, expected);

    const Money m{minorUnits, Currency::Sek};
    QCOMPARE(m.scaledBy(Percentage::fromBasisPoints(basisPoints)).minorUnits(), expected);
}

void TestMoney::scalesHalfAwayFromZero_data()
{
    QTest::addColumn<qint64>("minorUnits");
    QTest::addColumn<qint64>("expected");

    // 50 % of these: exactly .5 must go away from zero, not to even.
    QTest::newRow("positive half") << qint64(101) << qint64(51);
    QTest::newRow("negative half") << qint64(-101) << qint64(-51);
}

void TestMoney::scalesHalfAwayFromZero()
{
    QFETCH(qint64, minorUnits);
    QFETCH(qint64, expected);

    const Money m{minorUnits, Currency::Sek};
    QCOMPARE(m.scaledBy(Percentage::fromBasisPoints(5000)).minorUnits(), expected);
}

void TestMoney::splitsWithoutLosingMinorUnits_data()
{
    QTest::addColumn<qint64>("total");
    QTest::addColumn<int>("parts");

    QTest::newRow("even") << qint64(3000) << 3;
    QTest::newRow("indivisible") << qint64(1000) << 3;
    QTest::newRow("negative") << qint64(-1000) << 3;
    QTest::newRow("smaller than parts") << qint64(2) << 4;
    QTest::newRow("zero") << qint64(0) << 3;
}

void TestMoney::splitsWithoutLosingMinorUnits()
{
    QFETCH(qint64, total);
    QFETCH(int, parts);

    const Money m{total, Currency::Sek};

    qint64 sum = 0;
    for (int i = 0; i < parts; ++i)
        sum += m.splitShare(i, parts);

    // The defining property: shares always reconstruct the original exactly.
    QCOMPARE(sum, total);
}

void TestMoney::formatsForLogs()
{
    QCOMPARE(QString::fromStdString(Money{123456, Currency::Sek}.toString()),
             QStringLiteral("1234.56 SEK"));
    QCOMPARE(QString::fromStdString(Money{-5, Currency::Eur}.toString()),
             QStringLiteral("-0.05 EUR"));
    QCOMPARE(QString::fromStdString(Money{0, Currency::Usd}.toString()),
             QStringLiteral("0.00 USD"));
}

QTEST_APPLESS_MAIN(TestMoney)

#include "tst_money.moc"
