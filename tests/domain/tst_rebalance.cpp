// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/bucket.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/rebalance.h"
#include "domain/targets.h"

#include <QString>
#include <QTest>
#include <cstddef>
#include <cstdint>
#include <string>
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

std::size_t indexOf(Bucket bucket)
{
    return static_cast<std::size_t>(bucket);
}

TargetInputs householdWithGrowth(Money growthBalance)
{
    // 20 000 kr a month, 240 000 kr a year: the worked household the targets
    // tests use, so the numbers here can be read against theirs.
    TargetInputs inputs{Currency::Sek};
    inputs.monthlyExpenses = sek(2000000);
    inputs.annualExpenses = sek(24000000);
    inputs.lumpSumsNextThreeYears = sek(0);
    inputs.growthBalance = growthBalance;
    inputs.riskTolerance = Percentage::fromBasisPoints(5000);
    return inputs;
}

/// Buffer 10 000 - 120 000 kr with 65 000 kr as the chosen point, and Play
/// capped at 10 % of the Growth balance passed in.
ResolvedTargets buildingTable(Money growthBalance)
{
    return resolve(policyFor(Phase::BuildingWealth).targets(), householdWithGrowth(growthBalance));
}

/// Buffer 240 000 - 1 200 000 kr and Preserve 6 000 000 - 8 400 000 kr.
ResolvedTargets preservingTable()
{
    return resolve(policyFor(Phase::Preserving).targets(), householdWithGrowth(sek(0)));
}

RebalanceInput accumulating(Money buffer, Money growth, Money play)
{
    RebalanceInput input;
    input.phase = Phase::BuildingWealth;
    input.balances[indexOf(Bucket::Buffer)] = buffer;
    input.balances[indexOf(Bucket::Growth)] = growth;
    input.balances[indexOf(Bucket::Play)] = play;
    input.targets = buildingTable(growth);
    input.monthlySurplus = sek(1000000); // 10 000 kr
    return input;
}

RebalanceInput preserving(Money buffer, Money preserve, Money growth)
{
    RebalanceInput input;
    input.phase = Phase::Preserving;
    input.balances[indexOf(Bucket::Buffer)] = buffer;
    input.balances[indexOf(Bucket::Preserve)] = preserve;
    input.balances[indexOf(Bucket::Growth)] = growth;
    input.targets = preservingTable();
    return input;
}

bool mentionsMarketDown(const std::string& rationale)
{
    return QString::fromStdString(rationale).contains("market is down", Qt::CaseInsensitive);
}

/// The three promises a proposal makes: real amounts, no bucket paying itself,
/// and no sale out of Growth into a fall.
bool transfersAreLegal(const RebalanceProposal& proposal, bool marketDown)
{
    for (const Transfer& transfer : proposal.transfers) {
        if (!transfer.amount.isPositive())
            return false;
        if (transfer.from == transfer.to)
            return false;
        if (marketDown && transfer.from == Bucket::Growth)
            return false;
    }
    return true;
}

} // namespace

class TestRebalance : public QObject {
    Q_OBJECT

private slots:
    void accumulatingFillsTheBufferFirst();
    void bufferOutranksPlayWhileItIsLow();
    void accumulatingTrimsPlayAboveItsCeiling();
    void accumulatingLeavesPlayAtItsCeiling();
    void accumulatingSweepsBufferAboveItsBand();
    void accumulatingSendsFreeMoneyToGrowth();
    void preservingRefillsBufferFromPreserve();
    void preservingRefillsBufferFromGrowthWhenPreserveIsLow();
    void preservingNeverSellsGrowthIntoAFall();
    void preservingSplitsPreserveSurplus();
    void preservingSendsPreserveSurplusToGrowthWhenBufferIsFull();
    void preservingMovesNothingWhenNothingIsOutOfBalance();
    void nothingIsMovedFromATargetWithNoExcess();
    void proposalsNeverBreakTheTransferRules();
};

void TestRebalance::accumulatingFillsTheBufferFirst()
{
    // 5 000 kr in the buffer, below the 10 000 kr floor of its band.
    const RebalanceProposal proposal = planRebalance(accumulating(sek(500000), sek(0), sek(0)));

    QVERIFY(proposal.nextContribution.has_value());
    QCOMPARE(*proposal.nextContribution, Bucket::Buffer);
    QVERIFY(proposal.transfers.empty());
    QVERIFY(QString::fromStdString(proposal.rationale).contains("buffer comes first"));
}

void TestRebalance::bufferOutranksPlayWhileItIsLow()
{
    // Play is 20 000 kr above its ceiling and the buffer is still short: the
    // buffer wins, because that is the order the model fills them in.
    const RebalanceProposal proposal =
        planRebalance(accumulating(sek(500000), sek(50000000), sek(7000000)));

    QCOMPARE(*proposal.nextContribution, Bucket::Buffer);
    QVERIFY(proposal.transfers.empty());
}

void TestRebalance::accumulatingTrimsPlayAboveItsCeiling()
{
    // Growth is 500 000 kr, so Play's ceiling is 50 000 kr and 70 000 kr sits
    // in Play. The buffer is inside its band, so Play is next in line.
    const RebalanceProposal proposal =
        planRebalance(accumulating(sek(5000000), sek(50000000), sek(7000000)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Play);
    QCOMPARE(proposal.transfers.front().to, Bucket::Growth);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(2000000)); // 20 000 kr
    QCOMPARE(*proposal.nextContribution, Bucket::Growth);
    QVERIFY(QString::fromStdString(proposal.rationale).contains("ceiling"));
}

void TestRebalance::accumulatingLeavesPlayAtItsCeiling()
{
    // Exactly at the ceiling is inside it: the model states a limit, not a
    // prompt to shave the last krona off.
    const RebalanceProposal proposal =
        planRebalance(accumulating(sek(5000000), sek(50000000), sek(5000000)));

    QVERIFY(proposal.transfers.empty());
    QCOMPARE(*proposal.nextContribution, Bucket::Growth);
}

void TestRebalance::accumulatingSweepsBufferAboveItsBand()
{
    // 200 000 kr in a buffer whose band tops out at 120 000 kr, with 65 000 kr
    // as the point the household chose.
    const RebalanceProposal proposal =
        planRebalance(accumulating(sek(20000000), sek(100000000), sek(0)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Buffer);
    QCOMPARE(proposal.transfers.front().to, Bucket::Growth);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(13500000)); // 135 000 kr
    QCOMPARE(*proposal.nextContribution, Bucket::Growth);
}

void TestRebalance::accumulatingSendsFreeMoneyToGrowth()
{
    // A buffer inside its band and Play inside its ceiling: nothing to move, so
    // all of it goes where freedom is built.
    const RebalanceProposal proposal = planRebalance(accumulating(sek(5000000), sek(0), sek(0)));

    QVERIFY(proposal.transfers.empty());
    QVERIFY(proposal.nextContribution.has_value());
    QCOMPARE(*proposal.nextContribution, Bucket::Growth);
    QVERIFY(!proposal.rationale.empty());
}

void TestRebalance::preservingRefillsBufferFromPreserve()
{
    // Buffer 200 000 kr against a 240 000 kr floor; Preserve sits mid-band, so
    // it is the one that pays the 40 000 kr shortfall.
    const RebalanceProposal proposal =
        planRebalance(preserving(sek(20000000), sek(700000000), sek(100000000)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Preserve);
    QCOMPARE(proposal.transfers.front().to, Bucket::Buffer);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(4000000)); // 40 000 kr
    // No free money exists in a drawdown, so nothing is allocated.
    QVERIFY(!proposal.nextContribution.has_value());
}

void TestRebalance::preservingRefillsBufferFromGrowthWhenPreserveIsLow()
{
    // Both defence buckets are short: Growth is the only one that can help while
    // the market is not falling.
    const RebalanceProposal proposal =
        planRebalance(preserving(sek(20000000), sek(500000000), sek(100000000)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Growth);
    QCOMPARE(proposal.transfers.front().to, Bucket::Buffer);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(4000000));
    QVERIFY(!proposal.nextContribution.has_value());
    QVERIFY(!mentionsMarketDown(proposal.rationale));
}

void TestRebalance::preservingNeverSellsGrowthIntoAFall()
{
    RebalanceInput input = preserving(sek(20000000), sek(500000000), sek(100000000));
    input.marketDown = true;

    const RebalanceProposal proposal = planRebalance(input);

    // The model's one prohibition: Growth is not sold into a fall, so Preserve
    // pays even though it is below its own band.
    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Preserve);
    QCOMPARE(proposal.transfers.front().to, Bucket::Buffer);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(4000000));
    QVERIFY(mentionsMarketDown(proposal.rationale));
    QVERIFY(transfersAreLegal(proposal, true));
}

void TestRebalance::preservingSplitsPreserveSurplus()
{
    // Preserve is 2 600 000 kr over its ceiling and the buffer has 700 000 kr of
    // room to its own ceiling, so the excess fills the buffer and the rest goes
    // to Growth.
    const RebalanceProposal proposal =
        planRebalance(preserving(sek(50000000), sek(1100000000), sek(0)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(2));

    QCOMPARE(proposal.transfers[0].from, Bucket::Preserve);
    QCOMPARE(proposal.transfers[0].to, Bucket::Buffer);
    QCOMPARE(ore(proposal.transfers[0].amount), qint64(70000000)); // 700 000 kr

    QCOMPARE(proposal.transfers[1].from, Bucket::Preserve);
    QCOMPARE(proposal.transfers[1].to, Bucket::Growth);
    QCOMPARE(ore(proposal.transfers[1].amount), qint64(190000000)); // 1 900 000 kr
    QVERIFY(!proposal.nextContribution.has_value());
}

void TestRebalance::preservingSendsPreserveSurplusToGrowthWhenBufferIsFull()
{
    // The buffer is already above its ceiling, so none of the excess stops there.
    const RebalanceProposal proposal =
        planRebalance(preserving(sek(150000000), sek(900000000), sek(0)));

    QCOMPARE(proposal.transfers.size(), static_cast<std::size_t>(1));
    QCOMPARE(proposal.transfers.front().from, Bucket::Preserve);
    QCOMPARE(proposal.transfers.front().to, Bucket::Growth);
    QCOMPARE(ore(proposal.transfers.front().amount), qint64(60000000)); // 600 000 kr
}

void TestRebalance::preservingMovesNothingWhenNothingIsOutOfBalance()
{
    const RebalanceProposal proposal =
        planRebalance(preserving(sek(50000000), sek(700000000), sek(0)));

    QVERIFY(proposal.transfers.empty());
    QVERIFY(!proposal.nextContribution.has_value());
    QVERIFY(!proposal.rationale.empty());
}

void TestRebalance::nothingIsMovedFromATargetWithNoExcess()
{
    // A hand-built target whose point sits outside its own band: the balance is
    // above the band but not above the point, so there is nothing to move and no
    // zero-krona transfer is invented to say so.
    RebalanceInput input = accumulating(sek(20000000), sek(0), sek(0));
    ResolvedTarget& buffer = input.targets[indexOf(Bucket::Buffer)];
    buffer.band = MoneyRange{sek(1000000), sek(12000000)};
    buffer.point = sek(20000000);

    const RebalanceProposal proposal = planRebalance(input);

    QVERIFY(proposal.transfers.empty());
    QCOMPARE(*proposal.nextContribution, Bucket::Growth);
}

void TestRebalance::proposalsNeverBreakTheTransferRules()
{
    const std::vector<RebalanceInput> proposals{
        accumulating(sek(500000), sek(50000000), sek(7000000)),
        accumulating(sek(5000000), sek(50000000), sek(7000000)),
        accumulating(sek(20000000), sek(100000000), sek(500000)),
        accumulating(sek(5000000), sek(0), sek(0)),
        preserving(sek(20000000), sek(700000000), sek(100000000)),
        preserving(sek(20000000), sek(500000000), sek(100000000)),
        preserving(sek(50000000), sek(1100000000), sek(0)),
        preserving(sek(150000000), sek(900000000), sek(0)),
        preserving(sek(50000000), sek(700000000), sek(0)),
    };

    for (const RebalanceInput& input : proposals) {
        const RebalanceProposal proposal = planRebalance(input);
        QVERIFY(transfersAreLegal(proposal, input.marketDown));
        QVERIFY(!proposal.rationale.empty());

        // A drawdown phase never hands out free money.
        if (!isAccumulating(input.phase))
            QVERIFY(!proposal.nextContribution.has_value());

        RebalanceInput falling = input;
        falling.marketDown = true;
        const RebalanceProposal inFall = planRebalance(falling);
        QVERIFY(transfersAreLegal(inFall, true));
    }
}

QTEST_APPLESS_MAIN(TestRebalance)

#include "tst_rebalance.moc"
