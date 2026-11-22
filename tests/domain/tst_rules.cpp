// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/rules.h"

#include <QString>
#include <QStringList>
#include <QTest>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

using namespace hinkwise::domain;

namespace {

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

/// A household together with the metrics that belong to it.
///
/// The metrics are written by hand because computeMetrics() has its own test:
/// a rule test should fail only because of the rule it exercises.
struct Scenario {
    HouseholdFacts facts;
    Metrics metrics;
};

void setBucket(HouseholdFacts& facts, Bucket bucket, Money balance)
{
    facts.sheet.perBucket[static_cast<std::size_t>(bucket)] = balance;
}

void setBand(HouseholdFacts& facts, Bucket bucket, Money low, Money high)
{
    ResolvedTarget& target = facts.targets[static_cast<std::size_t>(bucket)];
    target.kind = BucketTarget::Kind::MultipleOfExpenses;
    target.band.low = low;
    target.band.high = high;
}

void setCap(HouseholdFacts& facts, Bucket bucket, Money cap)
{
    facts.targets[static_cast<std::size_t>(bucket)].cap = cap;
}

void clearCap(HouseholdFacts& facts, Bucket bucket)
{
    facts.targets[static_cast<std::size_t>(bucket)].cap.reset();
}

/// Trips none of the eleven rules: expenses are recorded, the buffer sits in
/// the middle of its band, play is inside its cap and nothing is extreme.
Scenario healthyHousehold()
{
    Scenario scenario;
    HouseholdFacts& facts = scenario.facts;
    Metrics& metrics = scenario.metrics;

    // 40 000 kr of income a month, split 40 / 20 / 40 including amortisation.
    facts.phase = Phase::BuildingWealth;
    facts.flow.income = sek(4000000);
    facts.flow.needs = sek(1600000);
    facts.flow.wants = sek(800000);
    facts.flow.saving = sek(1600000);
    facts.badLoanTotal = sek(0);

    // 20 000 kr of expenses a month: a buffer band of 0.5-6 months is
    // 10 000-120 000 kr, and 60 000 kr sits in the bucket.
    setBand(facts, Bucket::Buffer, sek(1000000), sek(12000000));
    setBucket(facts, Bucket::Buffer, sek(6000000));

    // Play is capped at 10 % of 400 000 kr of growth, and half of the cap is used.
    setBucket(facts, Bucket::Growth, sek(40000000));
    setCap(facts, Bucket::Play, sek(4000000));
    setBucket(facts, Bucket::Play, sek(2000000));

    metrics.monthlyExpenses = sek(2000000);
    metrics.annualExpenses = sek(24000000);
    metrics.needsShare = Percentage::fromBasisPoints(4000);
    metrics.wantsShare = Percentage::fromBasisPoints(2000);
    metrics.savingShare = Percentage::fromBasisPoints(4000);
    metrics.savingsRate = Percentage::fromBasisPoints(4000);
    metrics.crashImpact = Percentage::fromBasisPoints(500);

    return scenario;
}

std::vector<Insight> evaluate(const Scenario& scenario)
{
    return evaluateRules(scenario.facts, scenario.metrics);
}

bool fired(const std::vector<Insight>& insights, const char* id)
{
    return std::any_of(insights.begin(), insights.end(),
                       [id](const Insight& insight) { return insight.id == id; });
}

QString joinedIds(const std::vector<Insight>& insights)
{
    QStringList ids;
    for (const Insight& insight : insights)
        ids.append(QString::fromStdString(insight.id));
    return ids.join(QLatin1String(", "));
}

/// One perturbation of the healthy household. Each turns a baseline that trips
/// nothing into a household that trips exactly the rule under test.
using Perturbation = void (*)(Scenario& scenario);

namespace fires {

void expensesMissing(Scenario& scenario)
{
    scenario.metrics.annualExpenses = sek(0);
}

void bufferEmpty(Scenario& scenario)
{
    setBucket(scenario.facts, Bucket::Buffer, sek(0));
}

void bufferUnderfunded(Scenario& scenario)
{
    setBucket(scenario.facts, Bucket::Buffer, sek(500000)); // 5 000 kr, under half a month
}

void bufferOverfull(Scenario& scenario)
{
    setBucket(scenario.facts, Bucket::Buffer, sek(15000000)); // 150 000 kr, over six months
}

void playOverCap(Scenario& scenario)
{
    setBucket(scenario.facts, Bucket::Play, sek(6000000)); // 60 000 kr against a 40 000 kr cap
}

void badLoanWhileBufferThin(Scenario& scenario)
{
    setBucket(scenario.facts, Bucket::Buffer, sek(500000));
    scenario.facts.badLoanTotal = sek(5000000); // 50 000 kr of consumer debt
}

void savingsRateLow(Scenario& scenario)
{
    scenario.metrics.savingsRate = Percentage::fromBasisPoints(500);
}

void needsShareHigh(Scenario& scenario)
{
    scenario.metrics.needsShare = Percentage::fromBasisPoints(6000);
}

void preserveThinWhenPreserving(Scenario& scenario)
{
    scenario.facts.phase = Phase::Preserving;
    setBand(scenario.facts, Bucket::Preserve, sek(60000000), sek(84000000)); // 25-35 annual
    setBucket(scenario.facts, Bucket::Preserve, sek(1000000));               // 10 000 kr
}

void crashImpactHigh(Scenario& scenario)
{
    scenario.metrics.crashImpact = Percentage::fromBasisPoints(3000);
}

void phaseMaybeBuilding(Scenario& scenario)
{
    scenario.facts.phase = Phase::GettingStarted;
    setBucket(scenario.facts, Bucket::Buffer, sek(12000000)); // exactly the top of the band
}

} // namespace fires

Scenario perturbed(Perturbation perturbation)
{
    Scenario scenario = healthyHousehold();
    perturbation(scenario);
    return scenario;
}

/// Trips one rule of every severity, so the ordering can be checked end to end.
void manyProblems(Scenario& scenario)
{
    scenario.facts.phase = Phase::GettingStarted;
    setBucket(scenario.facts, Bucket::Buffer, sek(0));
    setBucket(scenario.facts, Bucket::Play, sek(6000000));
    scenario.facts.badLoanTotal = sek(5000000);
    scenario.metrics.annualExpenses = sek(0);
    scenario.metrics.savingsRate = Percentage::fromBasisPoints(500);
    scenario.metrics.needsShare = Percentage::fromBasisPoints(6000);
    scenario.metrics.crashImpact = Percentage::fromBasisPoints(3000);
}

struct FiringCase {
    Perturbation perturbation;
    const char* id;
};

std::array<FiringCase, 11> firingCases()
{
    return {{{fires::expensesMissing, "expenses-missing"},
             {fires::bufferEmpty, "buffer-empty"},
             {fires::bufferUnderfunded, "buffer-underfunded"},
             {fires::bufferOverfull, "buffer-overfull"},
             {fires::playOverCap, "play-over-cap"},
             {fires::badLoanWhileBufferThin, "bad-loan-while-buffer-thin"},
             {fires::savingsRateLow, "savings-rate-low"},
             {fires::needsShareHigh, "needs-share-high"},
             {fires::preserveThinWhenPreserving, "preserve-thin-when-preserving"},
             {fires::crashImpactHigh, "crash-impact-high"},
             {fires::phaseMaybeBuilding, "phase-maybe-building"}}};
}

} // namespace

class TestRules : public QObject {
    Q_OBJECT

private slots:
    void healthyHouseholdTripsNothing();

    void expensesMissingFires();
    void expensesMissingStaysQuietOnceExpensesExist();

    void bufferEmptyFires();
    void bufferEmptyStaysQuietWhenFunded();

    void bufferUnderfundedFires();
    void bufferUnderfundedStaysQuietAtTheLowEnd();
    void bufferUnderfundedStaysQuietWhenEmpty();

    void bufferOverfullFires();
    void bufferOverfullStaysQuietWhenPreserving();

    void playOverCapFires();
    void playOverCapStaysQuietWithoutACap();

    void badLoanWhileBufferThinFires();
    void badLoanStaysQuietWhenTheBufferIsFunded();

    void savingsRateLowFires();
    void savingsRateLowStaysQuietWithoutIncome();

    void needsShareHighFires();
    void needsShareHighStaysQuietAtTheSplit();

    void preserveThinWhenPreservingFires();
    void preserveThinStaysQuietWhileBuilding();

    void crashImpactHighFires();
    void crashImpactStaysQuietAtAQuarter();

    void phaseMaybeBuildingFires();
    void phaseMaybeBuildingStaysQuietBelowTheBandTop();

    void ordersBySeverityThenId();
    void everyInsightCarriesText();
    void everyRuleIdIsDocumented();
};

void TestRules::healthyHouseholdTripsNothing()
{
    const std::vector<Insight> insights = evaluate(healthyHousehold());

    QVERIFY2(insights.empty(), qPrintable(joinedIds(insights)));
}

void TestRules::expensesMissingFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::expensesMissing)), "expenses-missing"));
}

void TestRules::expensesMissingStaysQuietOnceExpensesExist()
{
    QVERIFY(!fired(evaluate(healthyHousehold()), "expenses-missing"));
}

void TestRules::bufferEmptyFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::bufferEmpty)), "buffer-empty"));
}

void TestRules::bufferEmptyStaysQuietWhenFunded()
{
    QVERIFY(!fired(evaluate(healthyHousehold()), "buffer-empty"));
}

void TestRules::bufferUnderfundedFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::bufferUnderfunded)), "buffer-underfunded"));
}

void TestRules::bufferUnderfundedStaysQuietWhenEmpty()
{
    // An empty buffer is Critical already; a Warning saying the same thing is
    // duplicate noise in the feed.
    const Scenario empty = perturbed(fires::bufferEmpty);
    QVERIFY(fired(evaluate(empty), "buffer-empty"));
    QVERIFY(!fired(evaluate(empty), "buffer-underfunded"));
}

void TestRules::bufferUnderfundedStaysQuietAtTheLowEnd()
{
    Scenario scenario = healthyHousehold();
    setBucket(scenario.facts, Bucket::Buffer, sek(1000000)); // the low end exactly

    QVERIFY(!fired(evaluate(scenario), "buffer-underfunded"));
}

void TestRules::bufferOverfullFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::bufferOverfull)), "buffer-overfull"));
}

void TestRules::bufferOverfullStaysQuietWhenPreserving()
{
    Scenario scenario = perturbed(fires::bufferOverfull);
    scenario.facts.phase = Phase::Preserving;

    QVERIFY(!fired(evaluate(scenario), "buffer-overfull"));
}

void TestRules::playOverCapFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::playOverCap)), "play-over-cap"));
}

void TestRules::playOverCapStaysQuietWithoutACap()
{
    Scenario scenario = perturbed(fires::playOverCap);
    clearCap(scenario.facts, Bucket::Play);

    QVERIFY(!fired(evaluate(scenario), "play-over-cap"));
}

void TestRules::badLoanWhileBufferThinFires()
{
    QVERIFY(
        fired(evaluate(perturbed(fires::badLoanWhileBufferThin)), "bad-loan-while-buffer-thin"));
}

void TestRules::badLoanStaysQuietWhenTheBufferIsFunded()
{
    Scenario scenario = healthyHousehold();
    scenario.facts.badLoanTotal = sek(5000000);

    QVERIFY(!fired(evaluate(scenario), "bad-loan-while-buffer-thin"));
}

void TestRules::savingsRateLowFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::savingsRateLow)), "savings-rate-low"));
}

void TestRules::savingsRateLowStaysQuietWithoutIncome()
{
    Scenario scenario = perturbed(fires::savingsRateLow);
    scenario.facts.flow.income = sek(0);

    QVERIFY(!fired(evaluate(scenario), "savings-rate-low"));
}

void TestRules::needsShareHighFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::needsShareHigh)), "needs-share-high"));
}

void TestRules::needsShareHighStaysQuietAtTheSplit()
{
    Scenario scenario = healthyHousehold();
    scenario.metrics.needsShare = Percentage::fromBasisPoints(5500);

    QVERIFY(!fired(evaluate(scenario), "needs-share-high"));
}

void TestRules::preserveThinWhenPreservingFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::preserveThinWhenPreserving)),
                  "preserve-thin-when-preserving"));
}

void TestRules::preserveThinStaysQuietWhileBuilding()
{
    Scenario scenario = perturbed(fires::preserveThinWhenPreserving);
    scenario.facts.phase = Phase::BuildingWealth;

    QVERIFY(!fired(evaluate(scenario), "preserve-thin-when-preserving"));
}

void TestRules::crashImpactHighFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::crashImpactHigh)), "crash-impact-high"));
}

void TestRules::crashImpactStaysQuietAtAQuarter()
{
    Scenario scenario = healthyHousehold();
    scenario.metrics.crashImpact = Percentage::fromBasisPoints(2500);

    QVERIFY(!fired(evaluate(scenario), "crash-impact-high"));
}

void TestRules::phaseMaybeBuildingFires()
{
    QVERIFY(fired(evaluate(perturbed(fires::phaseMaybeBuilding)), "phase-maybe-building"));
}

void TestRules::phaseMaybeBuildingStaysQuietBelowTheBandTop()
{
    Scenario scenario = healthyHousehold();
    scenario.facts.phase = Phase::GettingStarted;
    setBucket(scenario.facts, Bucket::Buffer, sek(11000000)); // 10 000 kr short of the top

    QVERIFY(!fired(evaluate(scenario), "phase-maybe-building"));
}

void TestRules::ordersBySeverityThenId()
{
    const std::vector<Insight> insights = evaluate(perturbed(manyProblems));

    // buffer-empty and buffer-underfunded are mutually exclusive: an empty
    // buffer is reported once, as Critical.
    const std::array<QString, 7> expected{
        QStringLiteral("buffer-empty"),
        QStringLiteral("expenses-missing"),
        QStringLiteral("bad-loan-while-buffer-thin"),
        QStringLiteral("play-over-cap"),
        QStringLiteral("crash-impact-high"),
        QStringLiteral("savings-rate-low"),
        QStringLiteral("needs-share-high"),
    };

    QVERIFY2(insights.size() == expected.size(), qPrintable(joinedIds(insights)));
    const std::size_t count = std::min(insights.size(), expected.size());
    for (std::size_t index = 0; index < count; ++index)
        QCOMPARE(QString::fromStdString(insights[index].id), expected[index]);

    for (std::size_t index = 1; index < insights.size(); ++index)
        QVERIFY(insights[index - 1].severity <= insights[index].severity);
}

void TestRules::everyInsightCarriesText()
{
    const std::vector<Insight> insights = evaluate(perturbed(manyProblems));

    QVERIFY(!insights.empty());
    for (const Insight& insight : insights) {
        QVERIFY(!insight.id.empty());
        QVERIFY(!insight.title.empty());
        QVERIFY(!insight.why.empty());
        QVERIFY(!insight.suggestion.empty());
    }
}

void TestRules::everyRuleIdIsDocumented()
{
    std::vector<std::string> seen;
    for (const FiringCase& firingCase : firingCases()) {
        const std::vector<Insight> insights = evaluate(perturbed(firingCase.perturbation));
        QVERIFY2(fired(insights, firingCase.id), firingCase.id);

        for (const Insight& insight : insights) {
            if (std::find(seen.begin(), seen.end(), insight.id) == seen.end())
                seen.push_back(insight.id);
        }
    }
    std::sort(seen.begin(), seen.end());

    // The ids the UI keys off, in the order the sort must produce.
    const std::array<QString, 11> documented{
        QStringLiteral("bad-loan-while-buffer-thin"),
        QStringLiteral("buffer-empty"),
        QStringLiteral("buffer-overfull"),
        QStringLiteral("buffer-underfunded"),
        QStringLiteral("crash-impact-high"),
        QStringLiteral("expenses-missing"),
        QStringLiteral("needs-share-high"),
        QStringLiteral("phase-maybe-building"),
        QStringLiteral("play-over-cap"),
        QStringLiteral("preserve-thin-when-preserving"),
        QStringLiteral("savings-rate-low"),
    };

    QCOMPARE(seen.size(), documented.size());
    const std::size_t count = std::min(seen.size(), documented.size());
    for (std::size_t index = 0; index < count; ++index)
        QCOMPARE(QString::fromStdString(seen[index]), documented[index]);
}

QTEST_APPLESS_MAIN(TestRules)

#include "tst_rules.moc"
