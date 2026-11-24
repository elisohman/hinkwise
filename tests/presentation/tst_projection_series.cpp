// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/bucket.h"
#include "presentation/projection_series.h"

#include <QTest>
#include <cstddef>
#include <cstdint>

using namespace hinkwise;
using namespace hinkwise::presentation;

namespace {

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
}

std::size_t indexOf(domain::Bucket bucket)
{
    return static_cast<std::size_t>(bucket);
}

/// A growth-only household that saves 12 000 kr a year at no real return, so
/// every expected figure can be worked out by hand.
domain::ProjectionInput input()
{
    domain::ProjectionInput projection;
    projection.years = 10;
    projection.startingBalances[indexOf(domain::Bucket::Growth)] = sek(10000000);
    projection.annualContributions[indexOf(domain::Bucket::Growth)] = sek(1200000);
    projection.annualExpenses = sek(20000000);
    projection.fiTarget = sek(20000000);
    return projection;
}

} // namespace

class TestProjectionSeries : public QObject {
    Q_OBJECT

private slots:
    void stacksTheBucketsUpOneAxis();
    void leavesRoomForTheTargetAboveThePeak();
    void marksTheYearTheTargetIsReached();
    void hasNoTargetWhenNoneIsStated();
    void refusesToDrawAnEmptyProjection();
    void carriesTheDebtAndNetWorthAlongTheHorizon();
    void reachesBelowZeroWhenDebtOutrunsTheCapital();
    void keepsNetWorthOnTheAxisAboveTheBands();
    void leavesTheDebtOutWhenNoneIsRecorded();
};

void TestProjectionSeries::stacksTheBucketsUpOneAxis()
{
    domain::ProjectionInput projection = input();
    projection.startingBalances[indexOf(domain::Bucket::Buffer)] = sek(1000000);
    projection.startingBalances[indexOf(domain::Bucket::Preserve)] = sek(2000000);
    projection.years = 1;

    const ProjectionSeries series = projectionSeriesOf(projection);

    QVERIFY(series.valid);
    QCOMPARE(series.bands.size(), qsizetype{2});
    QCOMPARE(series.bands.front().yearRatio, 0.0);
    QCOMPARE(series.bands.back().yearRatio, 1.0);

    // Today's tops are cumulative: buffer, then preserve, then growth, then
    // play. With no play balance the last two tops are the same.
    const ProjectionBand& today = series.bands.front();
    QVERIFY(today.bufferTop > 0.0);
    QVERIFY(today.preserveTop > today.bufferTop);
    QVERIFY(today.growthTop > today.preserveTop);
    QCOMPARE(today.playTop, today.growthTop);

    // A year of saving lifts the top of the stack.
    QVERIFY(series.bands.back().playTop > today.playTop);
    QVERIFY(series.bands.back().playTop < 1.0);
}

void TestProjectionSeries::leavesRoomForTheTargetAboveThePeak()
{
    // Five years of saving does not reach the target, so the target line sits
    // above the whole projection and the axis is sized by the target.
    domain::ProjectionInput projection = input();
    projection.years = 5;
    const ProjectionSeries series = projectionSeriesOf(projection);

    QVERIFY(series.valid);
    QVERIFY(series.hasTarget);
    QVERIFY(series.targetRatio > 0.0);
    QVERIFY(series.targetRatio < 1.0);

    // The axis stands for the target, which is higher than the projection, so
    // the final total is drawn below the target line rather than on the rim.
    QVERIFY(series.bands.back().playTop < series.targetRatio);

    // Five gridlines from zero to the top, the first one at the bottom edge.
    QCOMPARE(series.yTicks.size(), qsizetype{5});
    QCOMPARE(series.yTicks.front().ratio, 0.0);
    QCOMPARE(series.yTicks.front().value.minorUnits(), qint64{0});
    QCOMPARE(series.yTicks.back().ratio, 1.0);
    QVERIFY(series.yTicks.back().value > series.yTicks.at(2).value);
}

void TestProjectionSeries::marksTheYearTheTargetIsReached()
{
    // 100 000 kr plus 12 000 kr a year reaches 200 000 kr in year 9.
    const ProjectionSeries series = projectionSeriesOf(input());

    QCOMPARE(series.yearsToTarget, 9);
    QVERIFY(qAbs(series.targetYearRatio - 0.9) < 1e-9);
}

void TestProjectionSeries::hasNoTargetWhenNoneIsStated()
{
    domain::ProjectionInput projection = input();
    projection.fiTarget = sek(0);

    const ProjectionSeries series = projectionSeriesOf(projection);
    QVERIFY(series.valid);
    QVERIFY(!series.hasTarget);
    // A target of nothing is not reached "in year zero": there is no line to
    // place.
    QCOMPARE(series.yearsToTarget, -1);
}

void TestProjectionSeries::refusesToDrawAnEmptyProjection()
{
    domain::ProjectionInput projection;
    projection.years = 5;

    const ProjectionSeries series = projectionSeriesOf(projection);
    QVERIFY(!series.valid);
    QVERIFY(series.bands.isEmpty());
}

void TestProjectionSeries::carriesTheDebtAndNetWorthAlongTheHorizon()
{
    // 100 000 kr of capital and a 50 000 kr loan repaid at 10 000 kr a year:
    // the loan is gone in year five, and every year of it is a point on the
    // same axis as the buckets.
    domain::ProjectionInput projection = input();
    projection.startingDebt = sek(5000000);
    projection.annualAmortisation = sek(1000000);

    const ProjectionSeries series = projectionSeriesOf(projection);

    QVERIFY(series.valid);
    QVERIFY(series.hasDebt);
    QCOMPARE(series.debt.size(), series.bands.size());
    QCOMPARE(series.netWorth.size(), series.bands.size());

    QCOMPARE(series.debt.front().yearRatio, 0.0);
    QCOMPARE(series.debt.back().yearRatio, 1.0);
    QCOMPARE(series.debt.front().value.minorUnits(), qint64{5000000});
    QCOMPARE(series.debt[1].value.minorUnits(), qint64{4000000});
    QVERIFY(series.debt.front().ratio > series.debt[1].ratio);
    QCOMPARE(series.debt.back().ratio, 0.0);

    QCOMPARE(series.yearsToDebtFree, 5);
    // 100 000 kr of capital less today's 50 000 kr loan.
    QCOMPARE(series.netWorth.front().value.minorUnits(), qint64{5000000});
    QVERIFY(series.netWorth.back().ratio > series.netWorth.front().ratio);
    // Nothing sits below zero here, so the axis still starts at the bottom.
    QCOMPARE(series.zeroRatio, 0.0);
}

void TestProjectionSeries::reachesBelowZeroWhenDebtOutrunsTheCapital()
{
    // A 300 000 kr loan against 100 000 kr of capital: net worth is negative
    // today, and the line has to be on the chart rather than below its rim.
    domain::ProjectionInput projection = input();
    projection.startingDebt = sek(30000000);
    projection.annualAmortisation = sek(500000);

    const ProjectionSeries series = projectionSeriesOf(projection);

    QVERIFY(series.valid);
    QVERIFY(series.hasDebt);
    QVERIFY(series.zeroRatio > 0.0);
    QVERIFY(series.zeroRatio < 1.0);

    QVERIFY(series.netWorth.front().value.minorUnits() < 0);
    QVERIFY(series.netWorth.front().ratio < series.zeroRatio);
    QVERIFY(series.netWorth.front().ratio > 0.0);

    // The loan is the highest figure in the projection and is drawn under the
    // top of the axis, above the zero line.
    QVERIFY(series.debt.front().ratio < 1.0);
    QVERIFY(series.debt.front().ratio > series.zeroRatio);
    QCOMPARE(series.debt.front().value.minorUnits(), qint64{30000000});

    // Ten years of 5 000 kr does not clear 300 000 kr.
    QCOMPARE(series.yearsToDebtFree, -1);

    // The ticks start below zero, so the axis labels say where the net-worth
    // line lives instead of leaving it to be guessed.
    QVERIFY(series.yTicks.front().value.minorUnits() < 0);
    QCOMPARE(series.yTicks.front().ratio, 0.0);
    QCOMPARE(series.yTicks.back().ratio, 1.0);
}

void TestProjectionSeries::keepsNetWorthOnTheAxisAboveTheBands()
{
    // A household with a 400 000 kr flat against 100 000 kr of capital: net
    // worth counts the flat, so it is the highest figure on the chart and the
    // axis has to reach it. A line clamped to the rim would draw a household
    // that owns a flat as though it owned the bands alone.
    domain::ProjectionInput projection = input();
    projection.startingOtherAssets = sek(40000000);
    projection.otherAssetsRealReturn = domain::Percentage::fromBasisPoints(250);

    const ProjectionSeries series = projectionSeriesOf(projection);

    QVERIFY(series.valid);
    QVERIFY(series.netWorth.front().value > series.debt.front().value);
    QVERIFY(series.netWorth.front().ratio > series.bands.front().playTop);
    QVERIFY(series.netWorth.front().ratio < 1.0);
    QVERIFY(series.yTicks.back().value >= series.netWorth.front().value);
    // Nothing sits below zero, so the axis still starts at the bottom.
    QCOMPARE(series.zeroRatio, 0.0);
}

void TestProjectionSeries::leavesTheDebtOutWhenNoneIsRecorded()
{
    const ProjectionSeries series = projectionSeriesOf(input());

    QVERIFY(series.valid);
    QVERIFY(!series.hasDebt);
    QCOMPARE(series.yearsToDebtFree, -1);
    QCOMPARE(series.zeroRatio, 0.0);
    QCOMPARE(series.debt.front().value.minorUnits(), qint64{0});
    // With no debt, net worth is the total the bands already draw.
    QCOMPARE(series.netWorth.front().ratio, series.bands.front().playTop);
}

QTEST_APPLESS_MAIN(TestProjectionSeries)

#include "tst_projection_series.moc"
