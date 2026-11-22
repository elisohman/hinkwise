// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/percentage.h"

#include <QTest>

using namespace hinkwise::domain;

class TestPercentage : public QObject {
    Q_OBJECT

private slots:
    void convertsFromPercent_data();
    void convertsFromPercent();
    void storesBasisPoints();
    void addsAndSubtracts();
    void comparesAndClamps();
};

void TestPercentage::convertsFromPercent_data()
{
    QTest::addColumn<double>("percent");
    QTest::addColumn<int>("basisPoints");

    QTest::newRow("zero") << 0.0 << 0;
    QTest::newRow("twenty") << 20.0 << 2000;
    QTest::newRow("fractional") << 5.5 << 550;
    QTest::newRow("small") << 0.25 << 25;
    QTest::newRow("rounds up") << 0.555 << 56;
    QTest::newRow("rounds down") << 0.554 << 55;
    QTest::newRow("negative") << -3.5 << -350;
    QTest::newRow("isk tax") << 1.0 << 100;
}

void TestPercentage::convertsFromPercent()
{
    QFETCH(double, percent);
    QFETCH(int, basisPoints);

    QCOMPARE(Percentage::fromPercent(percent).basisPoints(), basisPoints);
}

void TestPercentage::storesBasisPoints()
{
    const Percentage half = Percentage::fromBasisPoints(5000);
    QCOMPARE(half.basisPoints(), 5000);
    QCOMPARE(half.asFraction(), 0.5);
    QVERIFY(!half.isZero());
    QVERIFY(!half.isNegative());

    const Percentage nil;
    QVERIFY(nil.isZero());
    QCOMPARE(nil.basisPoints(), 0);
}

void TestPercentage::addsAndSubtracts()
{
    const Percentage a = Percentage::fromBasisPoints(2000);
    const Percentage b = Percentage::fromBasisPoints(1500);

    QCOMPARE((a + b).basisPoints(), 3500);
    QCOMPARE((a - b).basisPoints(), 500);
    QCOMPARE((-a).basisPoints(), -2000);
}

void TestPercentage::comparesAndClamps()
{
    const Percentage low = Percentage::fromBasisPoints(100);
    const Percentage high = Percentage::fromBasisPoints(900);

    QVERIFY(low < high);
    QVERIFY(high > low);
    QVERIFY(low <= low);
    QVERIFY(high >= high);
    QVERIFY(low == Percentage::fromBasisPoints(100));

    QCOMPARE(clamp(Percentage::fromBasisPoints(50), low, high).basisPoints(), 100);
    QCOMPARE(clamp(Percentage::fromBasisPoints(500), low, high).basisPoints(), 500);
    QCOMPARE(clamp(Percentage::fromBasisPoints(5000), low, high).basisPoints(), 900);
}

QTEST_APPLESS_MAIN(TestPercentage)

#include "tst_percentage.moc"
