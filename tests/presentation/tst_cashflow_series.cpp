// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presentation/cashflow_series.h"

#include <QLocale>
#include <QTest>
#include <QVector>
#include <cstdint>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::presentation;

namespace {

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
}

domain::YearMonth month(int year, unsigned monthNumber)
{
    return domain::YearMonth{year, monthNumber};
}

application::MonthFlowRecord record(domain::YearMonth when, qint64 income, qint64 needs,
                                    qint64 wants, qint64 saving)
{
    return application::MonthFlowRecord{
        when, domain::MonthFlow{sek(income), sek(needs), sek(wants), sek(saving)}};
}

int indexOf(const QVector<FlowMonth>& months, domain::YearMonth wanted)
{
    for (int index = 0; index < months.size(); ++index) {
        if (months.at(index).month == wanted)
            return index;
    }
    return -1;
}

} // namespace

class TestCashflowSeries : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void splitsIncomeTheWayTheModelDoes();
    void saysHowFarTheSplitIsFromTheModel();
    void leavesMissingMonthsMissing();
    void ignoresRecordsInAnotherCurrency();
    void scalesTheSavingsRateToBothReferences();
    void hasNoSavingsSeriesWithoutAMonth();
};

void TestCashflowSeries::initTestCase()
{
    // The sentences are rounded through QLocale, so pin it: the assertions have
    // to read the same on every machine.
    QLocale::setDefault(QLocale{QStringLiteral("en_US")});
}

void TestCashflowSeries::splitsIncomeTheWayTheModelDoes()
{
    const IncomeSplit split =
        incomeSplitOf(domain::MonthFlow{sek(3000000), sek(1500000), sek(900000), sek(600000)});

    QVERIFY(split.hasIncome);
    QCOMPARE(split.needs.basisPoints(), std::int32_t{5000});
    QCOMPARE(split.wants.basisPoints(), std::int32_t{3000});
    QCOMPARE(split.saving.basisPoints(), std::int32_t{2000});

    // Without income there is nothing to divide, and the split says so rather
    // than claiming three shares of zero.
    const IncomeSplit empty = incomeSplitOf(domain::MonthFlow{});
    QVERIFY(!empty.hasIncome);
    QCOMPARE(empty.needs.basisPoints(), std::int32_t{0});
    QCOMPARE(empty.saving.basisPoints(), std::int32_t{0});
}

void TestCashflowSeries::saysHowFarTheSplitIsFromTheModel()
{
    QCOMPARE(deviationSentence(QStringLiteral("Needs"), true,
                               domain::Percentage::fromBasisPoints(5000), kNeedsTarget),
             QStringLiteral("Needs are on the model's 50."));
    QCOMPARE(deviationSentence(QStringLiteral("Needs"), true,
                               domain::Percentage::fromBasisPoints(5600), kNeedsTarget),
             QStringLiteral("Needs are 6 points above 50."));
    QCOMPARE(deviationSentence(QStringLiteral("Wants"), true,
                               domain::Percentage::fromBasisPoints(2100), kWantsTarget),
             QStringLiteral("Wants are 9 points below 30."));
    // Saving is one thing, not a plural, and the sentence agrees with it.
    QCOMPARE(deviationSentence(QStringLiteral("Saving"), false,
                               domain::Percentage::fromBasisPoints(2000), kSavingTarget),
             QStringLiteral("Saving is on the model's 20."));
    QCOMPARE(deviationSentence(QStringLiteral("Saving"), false,
                               domain::Percentage::fromBasisPoints(1200), kSavingTarget),
             QStringLiteral("Saving is 8 points below 20."));
}

void TestCashflowSeries::leavesMissingMonthsMissing()
{
    const std::vector<application::MonthFlowRecord> records{
        record(month(2026, 11), 3000000, 1500000, 900000, 600000),
        record(month(2026, 9), 2000000, 1000000, 600000, 400000),
        record(month(2026, 7), 1000000, 500000, 300000, 200000),
    };

    const QVector<FlowMonth> months =
        flowMonthsOf(records, month(2026, 11), 12, domain::Currency::Sek);

    QCOMPARE(months.size(), 12);
    QCOMPARE(months.front().month, month(2025, 12));
    QCOMPARE(months.back().month, month(2026, 11));

    QVERIFY(months.at(indexOf(months, month(2026, 11))).recorded);
    QVERIFY(months.at(indexOf(months, month(2026, 9))).recorded);
    QVERIFY(months.at(indexOf(months, month(2026, 7))).recorded);

    // The months between them are absent, not zero: no bar, no share.
    const FlowMonth& august = months.at(indexOf(months, month(2026, 8)));
    QVERIFY(!august.recorded);
    QCOMPARE(august.needsShare.basisPoints(), std::int32_t{0});

    // A month inside the window without a record is present and empty.
    QVERIFY(indexOf(months, month(2026, 6)) >= 0);
    QVERIFY(!months.at(indexOf(months, month(2026, 6))).recorded);

    // The split travels with the month it belongs to.
    QCOMPARE(months.at(indexOf(months, month(2026, 9))).savingShare.basisPoints(),
             std::int32_t{2000});
}

void TestCashflowSeries::ignoresRecordsInAnotherCurrency()
{
    const application::MonthFlowRecord euro{
        month(2026, 11), domain::MonthFlow{domain::Money{300000, domain::Currency::Eur},
                                           domain::Money{150000, domain::Currency::Eur},
                                           domain::Money{90000, domain::Currency::Eur},
                                           domain::Money{60000, domain::Currency::Eur}}};

    const std::vector<application::MonthFlowRecord> records{euro};
    const QVector<FlowMonth> months =
        flowMonthsOf(records, month(2026, 11), 12, domain::Currency::Sek);

    QCOMPARE(months.size(), 12);
    QVERIFY(!months.back().recorded);
}

void TestCashflowSeries::scalesTheSavingsRateToBothReferences()
{
    const std::vector<application::MonthFlowRecord> records{
        record(month(2026, 11), 3000000, 1500000, 900000, 600000), // 20 % saved
        record(month(2026, 9), 2000000, 1000000, 800000, 200000),  // 10 % saved
    };
    const QVector<FlowMonth> months =
        flowMonthsOf(records, month(2026, 11), 12, domain::Currency::Sek);

    const SavingsRateSeries series = savingsRateSeriesOf(months);

    QVERIFY(!series.empty);
    // 20 % fits, so the axis is the 40 % floor rather than the peak plus margin.
    QCOMPARE(series.axisMax.basisPoints(), std::int32_t{4000});
    QCOMPARE(series.points.size(), 2);

    // Points are placed across the whole window, so the line has a gap where a
    // month was never recorded.
    const double step = 1.0 / 11.0;
    QVERIFY(qAbs(series.points.at(0).x - 9 * step) < 1e-9);
    QVERIFY(qAbs(series.points.at(1).x - 11 * step) < 1e-9);
    QVERIFY(qAbs(series.points.at(0).y - 0.25) < 1e-9);
    QVERIFY(qAbs(series.points.at(1).y - 0.5) < 1e-9);

    // A rate above the references stretches the axis instead of being clipped.
    const std::vector<application::MonthFlowRecord> highRates{
        record(month(2026, 11), 1000000, 200000, 200000, 600000), // 60 %
    };
    const SavingsRateSeries high =
        savingsRateSeriesOf(flowMonthsOf(highRates, month(2026, 11), 12, domain::Currency::Sek));
    QVERIFY(high.axisMax > domain::Percentage::fromBasisPoints(6000));
    QVERIFY(high.points.at(0).y < 1.0);
}

void TestCashflowSeries::hasNoSavingsSeriesWithoutAMonth()
{
    const std::vector<application::MonthFlowRecord> records;
    const QVector<FlowMonth> months =
        flowMonthsOf(records, month(2026, 11), 12, domain::Currency::Sek);

    const SavingsRateSeries series = savingsRateSeriesOf(months);
    QVERIFY(series.empty);
    QVERIFY(series.points.isEmpty());
    // The references still have somewhere to sit, so an empty chart is not a
    // broken one.
    QCOMPARE(series.axisMax.basisPoints(), std::int32_t{2000});
}

QTEST_APPLESS_MAIN(TestCashflowSeries)

#include "tst_cashflow_series.moc"
