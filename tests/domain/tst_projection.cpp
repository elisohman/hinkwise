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

QTEST_APPLESS_MAIN(TestProjection)

#include "tst_projection.moc"
