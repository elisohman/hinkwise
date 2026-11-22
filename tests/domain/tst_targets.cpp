// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/phase.h"
#include "domain/targets.h"

#include <QTest>

using namespace hinkwise::domain;

namespace {

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

/// 20 000 kr a month, 240 000 kr a year.
TargetInputs typicalHousehold()
{
    TargetInputs inputs{Currency::Sek};
    inputs.monthlyExpenses = sek(2000000);
    inputs.annualExpenses = sek(24000000);
    inputs.lumpSumsNextThreeYears = sek(0);
    inputs.growthBalance = sek(0);
    inputs.riskTolerance = Percentage::fromBasisPoints(5000);
    return inputs;
}

} // namespace

class TestTargets : public QObject {
    Q_OBJECT

private slots:
    void accumulatingPhasesShareTheBuildingTable();
    void preservingPhasesShareTheFiveTwentyFiveTable();
    void flowReversesAtPreserving();
    void headlineFollowsPhase();
    void resolvesBufferInBuildingPhase();
    void resolvesBufferInPreservingPhase();
    void resolvesFiveTwentyFive();
    void playCapSurvivesEveryPhase_data();
    void playCapSurvivesEveryPhase();
    void interpolatesWithinTheBand_data();
    void interpolatesWithinTheBand();
    void clampsToleranceToTheBand_data();
    void clampsToleranceToTheBand();
    void noTargetStaysEmpty();
    void resolvesInNonSekCurrencies_data();
    void resolvesInNonSekCurrencies();
    void rejectsMixedCurrencies();
};

void TestTargets::accumulatingPhasesShareTheBuildingTable()
{
    for (const Phase phase : {Phase::GettingStarted, Phase::BuildingWealth}) {
        const BucketTargets& targets = policyFor(phase).targets();

        const auto& buffer = targets[Bucket::Buffer];
        QCOMPARE(buffer.kind, BucketTarget::Kind::MultipleOfExpenses);
        QCOMPARE(buffer.basis, ExpenseBasis::Monthly);
        // 0.5 - 6 months of expenses
        QCOMPARE(buffer.multiple.low.basisPoints(), 5000);
        QCOMPARE(buffer.multiple.high.basisPoints(), 60000);
        QVERIFY(buffer.includesLumpSums);

        QCOMPARE(targets[Bucket::Preserve].kind, BucketTarget::Kind::NoTarget);
        QCOMPARE(targets[Bucket::Growth].kind, BucketTarget::Kind::AsLargeAsPossible);
    }
}

void TestTargets::preservingPhasesShareTheFiveTwentyFiveTable()
{
    for (const Phase phase : {Phase::Preserving, Phase::FinanciallyIndependent}) {
        const BucketTargets& targets = policyFor(phase).targets();

        const auto& buffer = targets[Bucket::Buffer];
        QCOMPARE(buffer.kind, BucketTarget::Kind::MultipleOfExpenses);
        QCOMPARE(buffer.basis, ExpenseBasis::Annual);
        // 3 +- 2 years => 1 to 5 years
        QCOMPARE(buffer.multiple.low.basisPoints(), 10000);
        QCOMPARE(buffer.multiple.high.basisPoints(), 50000);
        QVERIFY(buffer.includesLumpSums);

        const auto& preserve = targets[Bucket::Preserve];
        QCOMPARE(preserve.kind, BucketTarget::Kind::MultipleOfExpenses);
        QCOMPARE(preserve.basis, ExpenseBasis::Annual);
        // 25 - 35 annual expenses
        QCOMPARE(preserve.multiple.low.basisPoints(), 250000);
        QCOMPARE(preserve.multiple.high.basisPoints(), 350000);

        // "If you have won the game, stop playing."
        QCOMPARE(targets[Bucket::Growth].kind, BucketTarget::Kind::NoTarget);
        QCOMPARE(targets[Bucket::Play].kind, BucketTarget::Kind::NoTarget);
    }
}

void TestTargets::flowReversesAtPreserving()
{
    QCOMPARE(policyFor(Phase::GettingStarted).flow(), FlowDirection::IntoBuckets);
    QCOMPARE(policyFor(Phase::BuildingWealth).flow(), FlowDirection::IntoBuckets);
    QCOMPARE(policyFor(Phase::Preserving).flow(), FlowDirection::OutOfBuckets);
    QCOMPARE(policyFor(Phase::FinanciallyIndependent).flow(), FlowDirection::OutOfBuckets);

    QVERIFY(isAccumulating(Phase::BuildingWealth));
    QVERIFY(!isAccumulating(Phase::Preserving));
}

void TestTargets::headlineFollowsPhase()
{
    QCOMPARE(policyFor(Phase::BuildingWealth).headline(), HeadlineMetric::YearsOfFreedom);
    QCOMPARE(policyFor(Phase::Preserving).headline(), HeadlineMetric::SafeWithdrawalRate);
    QCOMPARE(policyFor(Phase::FinanciallyIndependent).headline(),
             HeadlineMetric::SafeWithdrawalRate);
}

void TestTargets::resolvesBufferInBuildingPhase()
{
    const TargetInputs inputs = typicalHousehold();
    const ResolvedTargets resolved = resolve(policyFor(Phase::BuildingWealth).targets(), inputs);
    const ResolvedTarget& buffer = resolved[static_cast<std::size_t>(Bucket::Buffer)];

    // 0.5 month = 10 000 kr, 6 months = 120 000 kr
    QCOMPARE(buffer.band.low.minorUnits(), qint64(1000000));
    QCOMPARE(buffer.band.high.minorUnits(), qint64(12000000));
    // Midpoint tolerance: 65 000 kr
    QCOMPARE(buffer.point.minorUnits(), qint64(6500000));
}

void TestTargets::resolvesBufferInPreservingPhase()
{
    const TargetInputs inputs = typicalHousehold();
    const ResolvedTargets resolved = resolve(policyFor(Phase::Preserving).targets(), inputs);
    const ResolvedTarget& buffer = resolved[static_cast<std::size_t>(Bucket::Buffer)];

    // 1 to 5 years of 240 000 kr
    QCOMPARE(buffer.band.low.minorUnits(), qint64(24000000));
    QCOMPARE(buffer.band.high.minorUnits(), qint64(120000000));
}

void TestTargets::resolvesFiveTwentyFive()
{
    const TargetInputs inputs = typicalHousehold();
    const ResolvedTargets resolved = resolve(policyFor(Phase::Preserving).targets(), inputs);
    const ResolvedTarget& preserve = resolved[static_cast<std::size_t>(Bucket::Preserve)];

    // 25 - 35 x 240 000 kr
    QCOMPARE(preserve.band.low.minorUnits(), qint64(600000000));
    QCOMPARE(preserve.band.high.minorUnits(), qint64(840000000));
}

void TestTargets::playCapSurvivesEveryPhase_data()
{
    QTest::addColumn<int>("phase");

    QTest::newRow("getting started") << int(Phase::GettingStarted);
    QTest::newRow("building wealth") << int(Phase::BuildingWealth);
    QTest::newRow("preserving") << int(Phase::Preserving);
    QTest::newRow("financially independent") << int(Phase::FinanciallyIndependent);
}

void TestTargets::playCapSurvivesEveryPhase()
{
    QFETCH(int, phase);

    TargetInputs inputs = typicalHousehold();
    inputs.growthBalance = sek(50000000); // 500 000 kr

    const ResolvedTargets resolved =
        resolve(policyFor(static_cast<Phase>(phase)).targets(), inputs);
    const ResolvedTarget& play = resolved[static_cast<std::size_t>(Bucket::Play)];

    // Play is never a target...
    QVERIFY(!play.hasTarget());
    // ...but the 10 % ceiling applies in every phase, including the ones where
    // Play is optional. Dropping it there would let a play bucket grow unchecked
    // exactly when the household has stopped earning.
    QVERIFY(play.hasCap());
    QCOMPARE(play.cap->minorUnits(), qint64(5000000));
}

void TestTargets::interpolatesWithinTheBand_data()
{
    QTest::addColumn<int>("toleranceBasisPoints");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("cautious") << 0 << qint64(1000000);
    QTest::newRow("middle") << 5000 << qint64(6500000);
    QTest::newRow("bold") << 10000 << qint64(12000000);
}

void TestTargets::interpolatesWithinTheBand()
{
    QFETCH(int, toleranceBasisPoints);
    QFETCH(qint64, expected);

    TargetInputs inputs = typicalHousehold();
    inputs.riskTolerance = Percentage::fromBasisPoints(toleranceBasisPoints);

    const ResolvedTargets resolved = resolve(policyFor(Phase::BuildingWealth).targets(), inputs);
    QCOMPARE(resolved[static_cast<std::size_t>(Bucket::Buffer)].point.minorUnits(), expected);
}

void TestTargets::clampsToleranceToTheBand_data()
{
    QTest::addColumn<int>("toleranceBasisPoints");
    QTest::addColumn<qint64>("expected");

    // A slider bug must not push the point outside the model's band.
    QTest::newRow("over 100 %") << 15000 << qint64(12000000);
    QTest::newRow("negative") << -5000 << qint64(1000000);
}

void TestTargets::clampsToleranceToTheBand()
{
    QFETCH(int, toleranceBasisPoints);
    QFETCH(qint64, expected);

    TargetInputs inputs = typicalHousehold();
    inputs.riskTolerance = Percentage::fromBasisPoints(toleranceBasisPoints);

    const ResolvedTargets resolved = resolve(policyFor(Phase::BuildingWealth).targets(), inputs);
    QCOMPARE(resolved[static_cast<std::size_t>(Bucket::Buffer)].point.minorUnits(), expected);
}

void TestTargets::noTargetStaysEmpty()
{
    const TargetInputs inputs = typicalHousehold();

    const ResolvedTargets building = resolve(policyFor(Phase::BuildingWealth).targets(), inputs);
    QVERIFY(!building[static_cast<std::size_t>(Bucket::Preserve)].hasTarget());
    QVERIFY(!building[static_cast<std::size_t>(Bucket::Preserve)].hasCap());

    const ResolvedTargets preserving = resolve(policyFor(Phase::Preserving).targets(), inputs);
    QVERIFY(!preserving[static_cast<std::size_t>(Bucket::Growth)].hasTarget());
}

void TestTargets::resolvesInNonSekCurrencies_data()
{
    QTest::addColumn<int>("currency");

    QTest::newRow("EUR") << int(Currency::Eur);
    QTest::newRow("USD") << int(Currency::Usd);
}

void TestTargets::resolvesInNonSekCurrencies()
{
    QFETCH(int, currency);
    const auto code = static_cast<Currency>(currency);

    // A household whose expenses are in EUR must not inherit SEK zeroes and
    // throw on the first addition.
    TargetInputs inputs{code};
    inputs.monthlyExpenses = Money{2000000, code};
    inputs.annualExpenses = Money{24000000, code};
    inputs.growthBalance = Money{50000000, code};

    const ResolvedTargets resolved = resolve(policyFor(Phase::BuildingWealth).targets(), inputs);

    const ResolvedTarget& buffer = resolved[static_cast<std::size_t>(Bucket::Buffer)];
    QCOMPARE(buffer.band.low.minorUnits(), qint64(1000000));
    QCOMPARE(buffer.band.low.currency(), code);
    QCOMPARE(buffer.point.currency(), code);
    QCOMPARE(resolved[static_cast<std::size_t>(Bucket::Play)].cap->currency(), code);
}

void TestTargets::rejectsMixedCurrencies()
{
    TargetInputs inputs{Currency::Sek};
    inputs.monthlyExpenses = sek(2000000);
    inputs.annualExpenses = Money{24000000, Currency::Eur};

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             resolve(policyFor(Phase::BuildingWealth).targets(), inputs));
}

QTEST_APPLESS_MAIN(TestTargets)

#include "tst_targets.moc"
