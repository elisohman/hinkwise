// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/bucket.h"

#include <QTest>

using namespace hinkwise::domain;

/// These assertions are the contract with the model. If one of them fails,
/// the application is no longer an implementation of the four-bucket model,
/// so the expected values are quoted from the source rather than derived.
class TestBucket : public QObject {
    Q_OBJECT

private slots:
    void horizonsMatchTheModel();
    void expectedReturnsMatchTheModel();
    void riskBandsMatchTheModel();
    void defenceAndEngineAreSeparated();
    void playHasNoExpectedReturn();
    void orderingIsFillOrder();
};

void TestBucket::horizonsMatchTheModel()
{
    // Buffer: within 3 years. Preserve: 3-9 years. Growth: 10+ years.
    QCOMPARE(factsOf(Bucket::Buffer).horizon.minYears, 0);
    QCOMPARE(factsOf(Bucket::Buffer).horizon.maxYears.value(), 3);

    QCOMPARE(factsOf(Bucket::Preserve).horizon.minYears, 3);
    QCOMPARE(factsOf(Bucket::Preserve).horizon.maxYears.value(), 9);

    QCOMPARE(factsOf(Bucket::Growth).horizon.minYears, 10);
    QVERIFY(!factsOf(Bucket::Growth).horizon.hasUpperBound());

    QVERIFY(!factsOf(Bucket::Play).horizon.hasUpperBound());
}

void TestBucket::expectedReturnsMatchTheModel()
{
    const auto buffer = factsOf(Bucket::Buffer);
    QCOMPARE(buffer.nominalReturn.low.basisPoints(), 0);
    QCOMPARE(buffer.nominalReturn.high.basisPoints(), 200);
    QCOMPARE(buffer.realReturn.low.basisPoints(), -100);
    QCOMPARE(buffer.realReturn.high.basisPoints(), 0);

    const auto preserve = factsOf(Bucket::Preserve);
    QCOMPARE(preserve.nominalReturn.low.basisPoints(), 300);
    QCOMPARE(preserve.nominalReturn.high.basisPoints(), 600);
    QCOMPARE(preserve.realReturn.low.basisPoints(), 200);
    QCOMPARE(preserve.realReturn.high.basisPoints(), 300);

    const auto growth = factsOf(Bucket::Growth);
    QCOMPARE(growth.nominalReturn.low.basisPoints(), 600);
    QCOMPARE(growth.nominalReturn.high.basisPoints(), 900);
    QCOMPARE(growth.realReturn.low.basisPoints(), 500);
    QCOMPARE(growth.realReturn.high.basisPoints(), 700);
}

void TestBucket::riskBandsMatchTheModel()
{
    QCOMPARE(factsOf(Bucket::Buffer).riskBand.low, 1);
    QCOMPARE(factsOf(Bucket::Buffer).riskBand.high, 2);
    QCOMPARE(factsOf(Bucket::Preserve).riskBand.low, 3);
    QCOMPARE(factsOf(Bucket::Preserve).riskBand.high, 4);
    QCOMPARE(factsOf(Bucket::Growth).riskBand.low, 5);
    QCOMPARE(factsOf(Bucket::Growth).riskBand.high, 6);
}

void TestBucket::defenceAndEngineAreSeparated()
{
    QVERIFY(factsOf(Bucket::Buffer).defensive);
    QVERIFY(factsOf(Bucket::Preserve).defensive);
    QVERIFY(!factsOf(Bucket::Growth).defensive);
    QVERIFY(!factsOf(Bucket::Play).defensive);

    QVERIFY(isNearTerm(Bucket::Buffer));
    QVERIFY(!isNearTerm(Bucket::Preserve));
}

void TestBucket::playHasNoExpectedReturn()
{
    // The model states no expectation for Play. Zero here means "not stated",
    // which is why the UI must render it as an em dash rather than "0 %".
    const auto play = factsOf(Bucket::Play);
    QCOMPARE(play.realReturn.low.basisPoints(), 0);
    QCOMPARE(play.realReturn.high.basisPoints(), 0);
    QCOMPARE(play.riskBand.low, 1);
    QCOMPARE(play.riskBand.high, 7);
}

void TestBucket::orderingIsFillOrder()
{
    const auto buckets = allBuckets();
    QCOMPARE(buckets[0], Bucket::Buffer);
    QCOMPARE(buckets[1], Bucket::Preserve);
    QCOMPARE(buckets[2], Bucket::Growth);
    QCOMPARE(buckets[3], Bucket::Play);
    QCOMPARE(QString::fromUtf8(labelOf(Bucket::Preserve)), QStringLiteral("Preserve value"));
}

QTEST_APPLESS_MAIN(TestBucket)

#include "tst_bucket.moc"
