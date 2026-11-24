// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/asset.h"
#include "domain/balance_sheet.h"
#include "domain/bucket.h"
#include "domain/household.h"
#include "domain/jurisdiction.h"
#include "domain/metrics.h"
#include "domain/targets.h"

#include <QTest>
#include <array>
#include <cstddef>
#include <cstdint>

using namespace hinkwise::domain;

namespace {

/// Öre, like everywhere else in the domain. 1 000 kr == 100 000.
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

std::int32_t bucketShareOf(const Metrics& metrics, Bucket bucket)
{
    return bps(metrics.bucketShare[static_cast<std::size_t>(bucket)]);
}

/// Only the buffer carries a target the metrics read; the other three are left
/// as the no-target default.
ResolvedTargets targetsWithBufferPoint(Money point)
{
    ResolvedTargets targets{};
    ResolvedTarget& buffer = targets[static_cast<std::size_t>(Bucket::Buffer)];
    buffer.kind = BucketTarget::Kind::MultipleOfExpenses;
    buffer.band = MoneyRange{sek(1200000), point};
    buffer.point = point;
    return targets;
}

/// The worked household, in öre: 30 000 kr of income split 15 000 / 9 000 /
/// 6 000, 1 440 000 kr in the buckets, a 2 400 000 kr mortgage, and housing
/// plus pension worth another 3 360 000 kr on top of the buckets.
///
/// That makes 4 800 000 kr of assets and 2 400 000 kr of net worth, so the
/// bucket shares sum to 30 % and housing and pension make up the other 70 %.
HouseholdFacts workedHousehold()
{
    HouseholdFacts facts;

    facts.flow.income = sek(3000000);
    facts.flow.needs = sek(1500000);
    facts.flow.wants = sek(900000);
    facts.flow.saving = sek(600000);

    facts.sheet.totalAssets = sek(480000000);
    facts.sheet.totalLiabilities = sek(240000000);
    facts.sheet.netWorth = sek(240000000);
    facts.sheet.liquidAssets = sek(36000000);
    facts.sheet.headlineAssets = sek(144000000);
    facts.sheet.perBucket = {sek(7200000), sek(28800000), sek(96000000), sek(12000000)};

    // 6 months of expenses, the top of the building-phase band.
    facts.targets = targetsWithBufferPoint(sek(14400000));

    facts.jurisdiction = swedishJurisdiction();
    return facts;
}

} // namespace

class TestMetrics : public QObject {
    Q_OBJECT

private slots:
    void expensesAreNeedsPlusWants();
    void incomeSplitsIntoNeedsWantsAndSaving();
    void savingSplitIsBrokenDownAsSharesOfSaving();
    void savingSharesAreZeroWithoutABreakdown();
    void bufferMonthsCountsMonthsOfExpenses();
    void bufferCoverageComparesAgainstTheTargetPoint();
    void bufferCoverageIsZeroWithoutAPositiveTargetPoint();
    void bucketShareIsEachBucketOverTotalAssets();
    void financialAssetsExcludeHousingAndPension();
    void yearsOfFreedomCountsYearsOfExpenses();
    void fiTargetsScaleAnnualExpensesByJurisdiction();
    void fiProgressComparesAgainstTheDefaultTarget();
    void crashImpactIsHalfTheGrowthBucketOverNetWorth();
    void crashImpactIsZeroWithoutPositiveNetWorth();
    void pensionAndHousingShareIsOutsideFinancialAssets();
    void playShareOfGrowthIsPlayOverGrowth();
    void ratioRoundsHalfAwayFromZero();
    void sharesAreZeroWithoutIncome();
    void expensesAreZeroWithoutSpending();
    void sharesAreZeroWithoutAssets();
    void playShareIsZeroWithoutGrowth();
    void housingAndPensionDoNotCountAsFreedom();
};

void TestMetrics::expensesAreNeedsPlusWants()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(ore(metrics.monthlyExpenses), qint64(2400000)); // 24 000 kr
    QCOMPARE(ore(metrics.annualExpenses), qint64(28800000)); // 288 000 kr
}

void TestMetrics::incomeSplitsIntoNeedsWantsAndSaving()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(bps(metrics.needsShare), 5000);
    QCOMPARE(bps(metrics.wantsShare), 3000);
    QCOMPARE(bps(metrics.savingShare), 2000);
    QCOMPARE(bps(metrics.savingsRate), 2000);
}

void TestMetrics::savingSplitIsBrokenDownAsSharesOfSaving()
{
    HouseholdFacts facts = workedHousehold();
    // 6 000 kr of saving: 2 400 to the buffer, 2 400 to investing and 1 200 to
    // amortisation.
    facts.flow.savingSplit = SavingsBreakdown{sek(240000), sek(240000), sek(120000)};

    const Metrics metrics = computeMetrics(facts);

    QCOMPARE(bps(metrics.bufferShareOfSaving), 4000);
    QCOMPARE(bps(metrics.investingShareOfSaving), 4000);
    QCOMPARE(bps(metrics.amortisationShareOfSaving), 2000);

    // The total stays authoritative: the savings rate is still saving over
    // income with amortisation included, and breaking it down does not move it.
    QCOMPARE(bps(metrics.savingsRate), 2000);
    QCOMPARE(bps(metrics.savingShare), 2000);
}

void TestMetrics::savingSharesAreZeroWithoutABreakdown()
{
    // A month that recorded only its total says nothing about where the money
    // went, so none of the three shares is guessed.
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(bps(metrics.bufferShareOfSaving), 0);
    QCOMPARE(bps(metrics.investingShareOfSaving), 0);
    QCOMPARE(bps(metrics.amortisationShareOfSaving), 0);
    // The figures that only need the total are unaffected.
    QCOMPARE(bps(metrics.savingsRate), 2000);

    // A breakdown of nothing has no denominator either.
    HouseholdFacts facts = workedHousehold();
    facts.flow.saving = sek(0);
    facts.flow.savingSplit = SavingsBreakdown{sek(0), sek(0), sek(0)};

    const Metrics noSaving = computeMetrics(facts);
    QCOMPARE(bps(noSaving.bufferShareOfSaving), 0);
    QCOMPARE(bps(noSaving.investingShareOfSaving), 0);
    QCOMPARE(bps(noSaving.amortisationShareOfSaving), 0);
    QCOMPARE(bps(noSaving.savingsRate), 0);
}

void TestMetrics::bufferMonthsCountsMonthsOfExpenses()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 72 000 kr of buffer over 24 000 kr of monthly expenses: three months.
    QCOMPARE(bps(metrics.bufferMonths), 30000);
}

void TestMetrics::bufferCoverageComparesAgainstTheTargetPoint()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 72 000 kr against a 144 000 kr point is half of the target.
    QCOMPARE(bps(metrics.bufferCoverage), 5000);
}

void TestMetrics::bufferCoverageIsZeroWithoutAPositiveTargetPoint()
{
    HouseholdFacts facts = workedHousehold();
    facts.targets = ResolvedTargets{};
    QCOMPARE(bps(computeMetrics(facts).bufferCoverage), 0);

    facts.targets = targetsWithBufferPoint(sek(0));
    QCOMPARE(bps(computeMetrics(facts).bufferCoverage), 0);
}

void TestMetrics::bucketShareIsEachBucketOverTotalAssets()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(bucketShareOf(metrics, Bucket::Buffer), 150);   // 1.5 %
    QCOMPARE(bucketShareOf(metrics, Bucket::Preserve), 600); // 6 %
    QCOMPARE(bucketShareOf(metrics, Bucket::Growth), 2000);  // 20 %
    QCOMPARE(bucketShareOf(metrics, Bucket::Play), 250);     // 2.5 %
}

void TestMetrics::financialAssetsExcludeHousingAndPension()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(ore(metrics.financialAssets), qint64(144000000)); // 1 440 000 kr

    // Buying a 3 000 000 kr flat grows total assets but not the money the
    // household can act on. This is the case that the old bucket-summing
    // implementation got wrong.
    HouseholdFacts facts = workedHousehold();
    facts.sheet.perBucket[static_cast<std::size_t>(Bucket::Preserve)] += sek(300000000);
    facts.sheet.totalAssets += sek(300000000);

    const Metrics withHousing = computeMetrics(facts);
    QCOMPARE(ore(withHousing.financialAssets), qint64(144000000));
}

void TestMetrics::yearsOfFreedomCountsYearsOfExpenses()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 1 440 000 kr over 288 000 kr a year: five years.
    QCOMPARE(bps(metrics.yearsOfFreedom), 50000);
}

void TestMetrics::fiTargetsScaleAnnualExpensesByJurisdiction()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    QCOMPARE(ore(metrics.fiTargetLow), qint64(604800000));     // 288 000 x 21
    QCOMPARE(ore(metrics.fiTargetDefault), qint64(720000000)); // 288 000 x 25
    QCOMPARE(ore(metrics.fiTargetHigh), qint64(950400000));    // 288 000 x 33
}

void TestMetrics::fiProgressComparesAgainstTheDefaultTarget()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 1 440 000 kr of 7 200 000 kr: a fifth of the way.
    QCOMPARE(bps(metrics.fiProgress), 2000);
}

void TestMetrics::crashImpactIsHalfTheGrowthBucketOverNetWorth()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 960 000 kr of growth halved is 480 000 kr, over 2 400 000 kr of net
    // worth: a fifth of the households wealth.
    QCOMPARE(bps(metrics.crashImpact), 2000);
}

void TestMetrics::crashImpactIsZeroWithoutPositiveNetWorth()
{
    HouseholdFacts facts = workedHousehold();
    facts.sheet.netWorth = sek(0);
    QCOMPARE(bps(computeMetrics(facts).crashImpact), 0);

    facts.sheet.netWorth = sek(-150000000);
    QCOMPARE(bps(computeMetrics(facts).crashImpact), 0);
}

void TestMetrics::pensionAndHousingShareIsOutsideFinancialAssets()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 4 800 000 kr of assets minus 1 440 000 kr of buckets leaves 70 %.
    QCOMPARE(bps(metrics.pensionAndHousingShare), 7000);
}

void TestMetrics::playShareOfGrowthIsPlayOverGrowth()
{
    const Metrics metrics = computeMetrics(workedHousehold());

    // 120 000 kr of play against 960 000 kr of growth: 12.5 %.
    QCOMPARE(bps(metrics.playShareOfGrowth), 1250);
}

void TestMetrics::ratioRoundsHalfAwayFromZero()
{
    HouseholdFacts facts = workedHousehold();
    facts.flow.income = sek(20000); // 200 kr

    // 1 öre of 200 kr is 0.5 bp: half a basis point rounds up, not down.
    facts.flow.saving = sek(1);
    QCOMPARE(bps(computeMetrics(facts).savingsRate), 1);

    // And half a basis point below zero rounds away from zero too.
    facts.flow.saving = sek(-1);
    QCOMPARE(bps(computeMetrics(facts).savingsRate), -1);

    // A third of a basis point is still nothing.
    facts.flow.income = sek(30000);
    facts.flow.saving = sek(1);
    QCOMPARE(bps(computeMetrics(facts).savingsRate), 0);
}

void TestMetrics::sharesAreZeroWithoutIncome()
{
    HouseholdFacts facts = workedHousehold();
    facts.flow.income = sek(0);

    const Metrics metrics = computeMetrics(facts);

    QCOMPARE(bps(metrics.needsShare), 0);
    QCOMPARE(bps(metrics.wantsShare), 0);
    QCOMPARE(bps(metrics.savingShare), 0);
    QCOMPARE(bps(metrics.savingsRate), 0);
    // Spending is still what it is, so the expense figures do not move.
    QCOMPARE(ore(metrics.monthlyExpenses), qint64(2400000));
}

void TestMetrics::expensesAreZeroWithoutSpending()
{
    HouseholdFacts facts = workedHousehold();
    facts.flow.needs = sek(0);
    facts.flow.wants = sek(0);

    const Metrics metrics = computeMetrics(facts);

    QCOMPARE(ore(metrics.monthlyExpenses), qint64(0));
    QCOMPARE(ore(metrics.annualExpenses), qint64(0));
    QCOMPARE(bps(metrics.bufferMonths), 0);
    // No expenses means no cost of freedom, and no capital to aim for.
    QCOMPARE(bps(metrics.yearsOfFreedom), 0);
    QCOMPARE(ore(metrics.fiTargetLow), qint64(0));
    QCOMPARE(ore(metrics.fiTargetDefault), qint64(0));
    QCOMPARE(ore(metrics.fiTargetHigh), qint64(0));
    QCOMPARE(bps(metrics.fiProgress), 0);
}

void TestMetrics::sharesAreZeroWithoutAssets()
{
    HouseholdFacts facts = workedHousehold();
    facts.sheet.totalAssets = sek(0);
    facts.sheet.netWorth = sek(0);
    facts.sheet.headlineAssets = sek(0);
    facts.sheet.perBucket = {};

    const Metrics metrics = computeMetrics(facts);

    QCOMPARE(bucketShareOf(metrics, Bucket::Buffer), 0);
    QCOMPARE(bucketShareOf(metrics, Bucket::Preserve), 0);
    QCOMPARE(bucketShareOf(metrics, Bucket::Growth), 0);
    QCOMPARE(bucketShareOf(metrics, Bucket::Play), 0);
    QCOMPARE(ore(metrics.financialAssets), qint64(0));
    QCOMPARE(bps(metrics.pensionAndHousingShare), 0);
    QCOMPARE(bps(metrics.yearsOfFreedom), 0);
    QCOMPARE(bps(metrics.fiProgress), 0);
}

void TestMetrics::playShareIsZeroWithoutGrowth()
{
    HouseholdFacts facts = workedHousehold();
    facts.sheet.perBucket[static_cast<std::size_t>(Bucket::Growth)] = sek(0);

    const Metrics metrics = computeMetrics(facts);

    QCOMPARE(bps(metrics.playShareOfGrowth), 0);
    // Financial assets come from the balance sheet, not from the buckets, so
    // emptying a bucket does not move them.
    QCOMPARE(ore(metrics.financialAssets), qint64(144000000));
}

/// The integration path the hand-built fixtures cannot cover: a real balance
/// sheet built from assets, where the house and the pension sit inside
/// buckets. Missing this is what let financialAssets quietly equal total
/// assets and yearsOfFreedom count a flat as freedom.
void TestMetrics::housingAndPensionDoNotCountAsFreedom()
{
    const Asset bank = makeAsset("bank", "Bank", AssetKind::BankAccount);
    const Asset fund = makeAsset("fund", "Global index fund", AssetKind::Fund);
    const Asset house = makeAsset("house", "Flat", AssetKind::RealEstate);
    const Asset pension = makeAsset("pension", "Income pension", AssetKind::PensionIncome);
    const Liability mortgage = makeLiability("mortgage", "Mortgage", LiabilityKind::Mortgage,
                                             InterestRate::fromPartsPer100k(3500));

    const std::array<Holding, 4> holdings{
        Holding{&bank, sek(10000000)},    // 100 000 kr
        Holding{&fund, sek(40000000)},    // 400 000 kr
        Holding{&house, sek(300000000)},  // 3 000 000 kr
        Holding{&pension, sek(50000000)}, // 500 000 kr
    };
    const std::array<Debt, 1> debts{Debt{&mortgage, sek(200000000)}}; // 2 000 000 kr

    HouseholdFacts facts = workedHousehold();
    facts.sheet = buildBalanceSheet(holdings, debts);

    const Metrics metrics = computeMetrics(facts);

    // 4 000 000 kr of assets, of which only 500 000 kr is money you can act on.
    QCOMPARE(ore(metrics.financialAssets), qint64(50000000));

    // The flat and the pension are 87.5 % of everything owned...
    QCOMPARE(bps(metrics.pensionAndHousingShare), 8750);

    // ...and buy no freedom at all. 500 000 / 288 000 = 1.736 years.
    QCOMPARE(bps(metrics.yearsOfFreedom), 17361);

    // The house still counts as wealth.
    QCOMPARE(ore(facts.sheet.netWorth), qint64(200000000));
    QCOMPARE(ore(facts.sheet.totalAssets), qint64(400000000));
}

QTEST_APPLESS_MAIN(TestMetrics)

#include "tst_metrics.moc"
