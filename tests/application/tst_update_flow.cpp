// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/update_flow.h"
#include "domain/money.h"
#include "domain/year_month.h"

#include <QString>
#include <QTest>
#include <cstddef>
#include <optional>
#include <vector>

using namespace hinkwise::application;
using namespace hinkwise::domain;

namespace {

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

MoneyPoint point(int year, unsigned month, qint64 minorUnits)
{
    return MoneyPoint{YearMonth{year, month}, sek(minorUnits)};
}

QString monthOf(const std::optional<MoneyPoint>& found)
{
    if (!found)
        return {};
    return QString::fromStdString(found->month.toString());
}

} // namespace

class TestUpdateFlow : public QObject {
    Q_OBJECT

private slots:
    void recordableMonthsStartsAtTheCurrentMonthAndNeverRunsForward();
    void recordableMonthsStopsAtTheFirstRepresentableMonth();
    void amountInEffectCarriesTheLastRecordedValueForward();
    void amountInEffectIgnoresMonthsRecordedAfterTheOneAsked();
    void amountInEffectIsEmptyBeforeTheFirstRecording();
};

void TestUpdateFlow::recordableMonthsStartsAtTheCurrentMonthAndNeverRunsForward()
{
    const YearMonth current{2026, 3};
    const std::vector<YearMonth> months = recordableMonths(current, 3);

    QCOMPARE(months.size(), std::size_t(3));
    QCOMPARE(QString::fromStdString(months.at(0).toString()), QStringLiteral("2026-03"));
    QCOMPARE(QString::fromStdString(months.at(1).toString()), QStringLiteral("2026-02"));
    QCOMPARE(QString::fromStdString(months.at(2).toString()), QStringLiteral("2026-01"));

    // The year boundary is a step, not a special case.
    const std::vector<YearMonth> across = recordableMonths(YearMonth{2026, 1}, 2);
    QCOMPARE(QString::fromStdString(across.back().toString()), QStringLiteral("2025-12"));

    // Nothing offered is later than the current month, whatever the count.
    for (const YearMonth& month : recordableMonths(current, kRecordableMonths))
        QVERIFY(month <= current);

    QVERIFY(recordableMonths(current, 0).empty());
}

void TestUpdateFlow::recordableMonthsStopsAtTheFirstRepresentableMonth()
{
    // The list is bounded by the calendar the type can represent, so a count
    // that reaches past year 1 ends there instead of throwing.
    const std::vector<YearMonth> months = recordableMonths(YearMonth{1, 2}, 6);
    QCOMPARE(months.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(months.front().toString()), QStringLiteral("0001-02"));
    QCOMPARE(QString::fromStdString(months.back().toString()), QStringLiteral("0001-01"));
}

void TestUpdateFlow::amountInEffectCarriesTheLastRecordedValueForward()
{
    const std::vector<MoneyPoint> history{
        point(2026, 1, 100000),
        point(2026, 3, 250000),
        point(2026, 5, 400000),
    };

    // A month with its own recording is that recording.
    const std::optional<MoneyPoint> march = amountInEffectAt(history, YearMonth{2026, 3});
    QCOMPARE(monthOf(march), QStringLiteral("2026-03"));
    QCOMPARE(march->amount.minorUnits(), qint64(250000));

    // A month between two recordings is the earlier of them, never the later:
    // April's figure is March's until April is recorded.
    const std::optional<MoneyPoint> april = amountInEffectAt(history, YearMonth{2026, 4});
    QCOMPARE(monthOf(april), QStringLiteral("2026-03"));
    QCOMPARE(april->amount.minorUnits(), qint64(250000));

    // And a month after the last one carries the last one forward.
    const std::optional<MoneyPoint> july = amountInEffectAt(history, YearMonth{2026, 7});
    QCOMPARE(monthOf(july), QStringLiteral("2026-05"));
}

void TestUpdateFlow::amountInEffectIgnoresMonthsRecordedAfterTheOneAsked()
{
    const std::vector<MoneyPoint> history{
        point(2026, 1, 100000),
        point(2026, 3, 250000),
    };

    // Recording January again must not start from March's figure: the balance
    // in January was January's, and March's came later.
    const std::optional<MoneyPoint> january = amountInEffectAt(history, YearMonth{2026, 1});
    QCOMPARE(monthOf(january), QStringLiteral("2026-01"));
    QCOMPARE(january->amount.minorUnits(), qint64(100000));
}

void TestUpdateFlow::amountInEffectIsEmptyBeforeTheFirstRecording()
{
    const std::vector<MoneyPoint> history{point(2026, 3, 250000)};

    // Nothing is known about a month before the account was first recorded,
    // so the field starts empty rather than guessing zero.
    QVERIFY(!amountInEffectAt(history, YearMonth{2026, 2}).has_value());
    QVERIFY(!amountInEffectAt({}, YearMonth{2026, 3}).has_value());
}

QTEST_GUILESS_MAIN(TestUpdateFlow)
#include "tst_update_flow.moc"
