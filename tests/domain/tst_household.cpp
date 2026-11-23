// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/household.h"
#include "domain/money.h"

#include <QTest>
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

/// A month whose 10 000 kr of saving is recorded as 4 000 to the buffer,
/// 4 000 to investing and 2 000 to amortisation.
MonthFlow splitFlow()
{
    MonthFlow flow;
    flow.income = sek(3000000);
    flow.needs = sek(1500000);
    flow.wants = sek(500000);
    flow.saving = sek(1000000);
    flow.savingSplit = SavingsBreakdown{sek(400000), sek(400000), sek(200000)};
    return flow;
}

} // namespace

class TestHousehold : public QObject {
    Q_OBJECT

private slots:
    void savingBreakdownTotalsItsThreeParts();
    void savingBreakdownTotalsNegativeParts();
    void flowWithoutASplitAlwaysMatchesItsTotal();
    void flowWithASplitMatchesOnlyWhenThePartsAddUp();
    void flowWithAMixedCurrencySplitDoesNotMatchItsTotal();
};

void TestHousehold::savingBreakdownTotalsItsThreeParts()
{
    const SavingsBreakdown split{sek(400000), sek(400000), sek(200000)};

    QCOMPARE(ore(split.total()), qint64(1000000));
    QCOMPARE(static_cast<int>(split.total().currency()), static_cast<int>(Currency::Sek));

    // A breakdown is a breakdown: one part may be zero without the total moving.
    const SavingsBreakdown bufferOnly{sek(1000000), sek(0), sek(0)};
    QCOMPARE(ore(bufferOnly.total()), qint64(1000000));
}

void TestHousehold::savingBreakdownTotalsNegativeParts()
{
    // Nothing prevents a month with a negative saving, so the sum has to hold
    // for signed amounts too.
    const SavingsBreakdown split{sek(-400000), sek(-400000), sek(200000)};

    QCOMPARE(ore(split.total()), qint64(-600000));
}

void TestHousehold::flowWithoutASplitAlwaysMatchesItsTotal()
{
    // A month that records only its total is not in disagreement with anything:
    // the boundaries accept it, which is what keeps old rows loadable.
    MonthFlow flow;
    flow.saving = sek(1000000);
    QVERIFY(!flow.savingSplit.has_value());
    QVERIFY(savingSplitMatchesTotal(flow));

    flow.saving = sek(0);
    QVERIFY(savingSplitMatchesTotal(flow));
}

void TestHousehold::flowWithASplitMatchesOnlyWhenThePartsAddUp()
{
    MonthFlow flow = splitFlow();
    QVERIFY(savingSplitMatchesTotal(flow));

    // One part a single öre short of the total is still a mismatch: the
    // breakdown has to account for the whole saving, not most of it.
    flow.savingSplit->toAmortisation = sek(199999);
    QVERIFY(!savingSplitMatchesTotal(flow));

    // Each part can be the one that breaks the sum.
    flow = splitFlow();
    flow.savingSplit->toBuffer = sek(400001);
    QVERIFY(!savingSplitMatchesTotal(flow));

    flow = splitFlow();
    flow.savingSplit->toInvesting = sek(0);
    QVERIFY(!savingSplitMatchesTotal(flow));

    // The total is the authoritative figure and is not adjusted to fit.
    flow = splitFlow();
    QVERIFY(ore(flow.saving) == qint64(1000000));
}

void TestHousehold::flowWithAMixedCurrencySplitDoesNotMatchItsTotal()
{
    // 4 000 EUR is not 4 000 SEK, and adding the parts would throw. The two
    // boundaries report this as a load error, so the check answers false
    // instead.
    MonthFlow flow = splitFlow();
    flow.savingSplit->toInvesting = Money{400000, Currency::Eur};

    QVERIFY(!savingSplitMatchesTotal(flow));
}

QTEST_APPLESS_MAIN(TestHousehold)

#include "tst_household.moc"
