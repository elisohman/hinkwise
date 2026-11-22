// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/year_month.h"

#include <QTest>

using namespace hinkwise::domain;

class TestYearMonth : public QObject {
    Q_OBJECT

private slots:
    void indexes_data();
    void indexes();
    void advancesAcrossYearBoundaries_data();
    void advancesAcrossYearBoundaries();
    void countsMonthsBetween();
    void parsesValidInput_data();
    void parsesValidInput();
    void rejectsInvalidInput_data();
    void rejectsInvalidInput();
};

void TestYearMonth::indexes_data()
{
    QTest::addColumn<int>("year");
    QTest::addColumn<unsigned>("month");

    QTest::newRow("epoch") << 1970 << 1u;
    QTest::newRow("end of year") << 2026 << 12u;
    QTest::newRow("leap year") << 2024 << 2u;
    QTest::newRow("early") << 1930 << 5u;
}

void TestYearMonth::indexes()
{
    QFETCH(int, year);
    QFETCH(unsigned, month);

    const YearMonth ym{year, month};
    const YearMonth roundTripped = YearMonth::fromIndex(ym.index());

    QCOMPARE(roundTripped.year(), year);
    QCOMPARE(roundTripped.month(), month);
}

void TestYearMonth::advancesAcrossYearBoundaries_data()
{
    QTest::addColumn<int>("year");
    QTest::addColumn<unsigned>("month");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("expectedYear");
    QTest::addColumn<unsigned>("expectedMonth");

    QTest::newRow("forward over new year") << 2026 << 12u << 1 << 2027 << 1u;
    QTest::newRow("back over new year") << 2026 << 1u << -1 << 2025 << 12u;
    QTest::newRow("twelve months") << 2026 << 3u << 12 << 2027 << 3u;
    QTest::newRow("minus twenty-five") << 2026 << 3u << -25 << 2024 << 2u;
}

void TestYearMonth::advancesAcrossYearBoundaries()
{
    QFETCH(int, year);
    QFETCH(unsigned, month);
    QFETCH(int, offset);
    QFETCH(int, expectedYear);
    QFETCH(unsigned, expectedMonth);

    const YearMonth result = YearMonth{year, month}.plusMonths(offset);

    QCOMPARE(result.year(), expectedYear);
    QCOMPARE(result.month(), expectedMonth);
}

void TestYearMonth::countsMonthsBetween()
{
    const YearMonth jan2025{2025, 1u};
    const YearMonth jan2026{2026, 1u};
    const YearMonth nov2025{2025, 11u};
    const YearMonth feb2026{2026, 2u};

    QCOMPARE(YearMonth::monthsBetween(jan2025, jan2026), qint64(12));
    QCOMPARE(YearMonth::monthsBetween(jan2026, jan2025), qint64(-12));
    QCOMPARE(YearMonth::monthsBetween(jan2026, jan2026), qint64(0));
    QCOMPARE(YearMonth::monthsBetween(nov2025, feb2026), qint64(3));
}

void TestYearMonth::parsesValidInput_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("year");
    QTest::addColumn<unsigned>("month");

    QTest::newRow("canonical") << QStringLiteral("2026-11") << 2026 << 11u;
    QTest::newRow("january") << QStringLiteral("1970-01") << 1970 << 1u;
    QTest::newRow("december") << QStringLiteral("2030-12") << 2030 << 12u;
}

void TestYearMonth::parsesValidInput()
{
    QFETCH(QString, text);
    QFETCH(int, year);
    QFETCH(unsigned, month);

    const auto parsed = YearMonth::parse(text.toStdString());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->year(), year);
    QCOMPARE(parsed->month(), month);
    QCOMPARE(QString::fromStdString(parsed->toString()), text);
}

void TestYearMonth::rejectsInvalidInput_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << QString();
    QTest::newRow("too short") << QStringLiteral("2026-1");
    QTest::newRow("month zero") << QStringLiteral("2026-00");
    QTest::newRow("month thirteen") << QStringLiteral("2026-13");
    QTest::newRow("wrong separator") << QStringLiteral("2026/11");
    QTest::newRow("letters") << QStringLiteral("20a6-11");
    QTest::newRow("trailing") << QStringLiteral("2026-11-01");
}

void TestYearMonth::rejectsInvalidInput()
{
    QFETCH(QString, text);

    QVERIFY(!YearMonth::parse(text.toStdString()).has_value());
}

QTEST_APPLESS_MAIN(TestYearMonth)

#include "tst_year_month.moc"
