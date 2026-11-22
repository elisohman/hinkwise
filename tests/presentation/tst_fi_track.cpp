// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presentation/fi_track.h"

#include <QTest>

using namespace hinkwise;
using namespace hinkwise::presentation;

namespace {

domain::Metrics metricsAt(domain::Percentage progress)
{
    domain::Metrics metrics;
    metrics.fiProgress = progress;
    // x25 of 200 000 kr a year: the target itself only has to be positive, the
    // track measures ratios of it.
    metrics.fiTargetDefault = domain::Money{50000000, domain::Currency::Sek};
    return metrics;
}

} // namespace

class TestFiTrack : public QObject {
    Q_OBJECT

private slots:
    void placesTheBandUnderTheDefaultTarget();
    void movesProgressAlongTheSameAxis();
    void stretchesTheAxisForAHouseholdPastX25();
    void hasNoTrackWithoutATarget();
};

void TestFiTrack::placesTheBandUnderTheDefaultTarget()
{
    const FiTrack track = fiTrackOf(metricsAt(domain::Percentage::fromBasisPoints(4000)),
                                    domain::swedishJurisdiction());

    QVERIFY(track.known);
    QCOMPARE(track.lowMultiplier, 21);
    QCOMPARE(track.defaultMultiplier, 25);
    QCOMPARE(track.highMultiplier, 33);
    QVERIFY(track.lowRatio > 0.0);
    QVERIFY(track.lowRatio < track.defaultRatio);
    QVERIFY(track.defaultRatio < track.highRatio);
    QVERIFY(track.highRatio < 1.0);
}

void TestFiTrack::movesProgressAlongTheSameAxis()
{
    const FiTrack quarter = fiTrackOf(metricsAt(domain::Percentage::fromBasisPoints(2500)),
                                      domain::swedishJurisdiction());
    const FiTrack half = fiTrackOf(metricsAt(domain::Percentage::fromBasisPoints(5000)),
                                   domain::swedishJurisdiction());

    QVERIFY(quarter.progressRatio < half.progressRatio);
    QVERIFY(half.progressRatio < half.defaultRatio);
    // The band does not move when the household's own progress does: the ticks
    // are the jurisdiction's, not a rescaling of the balance.
    QCOMPARE(quarter.lowRatio, half.lowRatio);
    QCOMPARE(quarter.defaultRatio, half.defaultRatio);
}

void TestFiTrack::stretchesTheAxisForAHouseholdPastX25()
{
    const FiTrack track = fiTrackOf(metricsAt(domain::Percentage::fromBasisPoints(14000)),
                                    domain::swedishJurisdiction());

    // Past x25 the axis grows to hold the household, so the high tick moves
    // left instead of the progress going off the end.
    QVERIFY(track.progressRatio > track.defaultRatio);
    QVERIFY(track.progressRatio < 1.0);
    QVERIFY(track.highRatio < 1.0);
}

void TestFiTrack::hasNoTrackWithoutATarget()
{
    domain::Metrics metrics;
    const FiTrack track = fiTrackOf(metrics, domain::swedishJurisdiction());

    QVERIFY(!track.known);
    QCOMPARE(track.progressRatio, 0.0);
    QCOMPARE(track.defaultRatio, 0.0);
}

QTEST_APPLESS_MAIN(TestFiTrack)

#include "tst_fi_track.moc"
