// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presentation/input_text.h"

#include <QLocale>
#include <QTest>
#include <array>
#include <cstdint>

using namespace hinkwise;
using namespace hinkwise::presentation;

class TestInputText : public QObject {
    Q_OBJECT

private slots:
    void roundTripsThroughPlainText_data();
    void roundTripsThroughPlainText();
    void readsTheSeparatorTheHouseholdTyped_data();
    void readsTheSeparatorTheHouseholdTyped();
    void rejectsWhatIsNotAnAmount_data();
    void rejectsWhatIsNotAnAmount();
    void readsRatesAsBasisPoints();
    void rejectsRatesWithTooManyDecimals();
    void formatsWithTheCurrencyCode();
};

void TestInputText::roundTripsThroughPlainText_data()
{
    QTest::addColumn<QString>("locale");
    QTest::addColumn<qint64>("minorUnits");

    const std::array<QString, 3> locales{"sv_SE", "en_US", "de_DE"};
    const std::array<qint64, 8> amounts{0, 1, 99, 100, 123456, 123456789, 2500000, 999999999};
    for (const QString& locale : locales) {
        for (const qint64 amount : amounts) {
            const QString name = QStringLiteral("%1/%2").arg(locale).arg(amount);
            QTest::newRow(qPrintable(name)) << locale << amount;
        }
    }
}

void TestInputText::roundTripsThroughPlainText()
{
    QFETCH(QString, locale);
    QFETCH(qint64, minorUnits);

    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale{locale});

    const domain::Money amount{minorUnits, domain::Currency::Sek};
    const QString text = formatMoneyPlain(amount);
    const MoneyInput parsed = parseMoneyInput(text, domain::Currency::Sek);

    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.value.minorUnits(), amount.minorUnits());
    QCOMPARE(parsed.value.currency(), amount.currency());

    QLocale::setDefault(previous);
}

void TestInputText::readsTheSeparatorTheHouseholdTyped_data()
{
    QTest::addColumn<QString>("locale");
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("minorUnits");

    // A Swedish locale reading the English convention and the other way round,
    // which is what a household moving between keyboards actually types.
    QTest::newRow("sv grouping, comma decimal")
        << "sv_SE" << QStringLiteral("1 234,56") << qint64{123456};
    QTest::newRow("sv dot grouping") << "sv_SE" << QStringLiteral("1.234,56") << qint64{123456};
    QTest::newRow("sv comma without grouping")
        << "sv_SE" << QStringLiteral("1234,56") << qint64{123456};
    QTest::newRow("sv dot as decimal") << "sv_SE" << QStringLiteral("1234.56") << qint64{123456};
    QTest::newRow("en comma grouping, dot decimal")
        << "en_US" << QStringLiteral("1,234.56") << qint64{123456};
    QTest::newRow("en comma as decimal") << "en_US" << QStringLiteral("1234,56") << qint64{123456};
    QTest::newRow("en bare grouping") << "en_US" << QStringLiteral("1,234") << qint64{123400};
    QTest::newRow("sv bare dot grouping") << "sv_SE" << QStringLiteral("1.234") << qint64{123400};
    QTest::newRow("de grouping") << "de_DE" << QStringLiteral("1.234,56") << qint64{123456};
    QTest::newRow("padded decimals") << "sv_SE" << QStringLiteral("12,5") << qint64{1250};
    QTest::newRow("trailing separator") << "sv_SE" << QStringLiteral("100.") << qint64{10000};
    QTest::newRow("leading separator") << "sv_SE" << QStringLiteral(",5") << qint64{50};
    QTest::newRow("plain integer") << "sv_SE" << QStringLiteral("12000") << qint64{1200000};
    QTest::newRow("spaces and comma")
        << "sv_SE" << QStringLiteral(" 25 000,00 ") << qint64{2500000};
}

void TestInputText::readsTheSeparatorTheHouseholdTyped()
{
    QFETCH(QString, locale);
    QFETCH(QString, text);
    QFETCH(qint64, minorUnits);

    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale{locale});

    const MoneyInput parsed = parseMoneyInput(text, domain::Currency::Sek);
    QVERIFY2(parsed.ok, qPrintable(parsed.error + QLatin1Char(' ') + text));
    QCOMPARE(parsed.value.minorUnits(), minorUnits);

    QLocale::setDefault(previous);
}

void TestInputText::rejectsWhatIsNotAnAmount_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << QString{};
    QTest::newRow("spaces only") << QStringLiteral("   ");
    QTest::newRow("negative") << QStringLiteral("-100");
    QTest::newRow("letters") << QStringLiteral("100 kr");
    QTest::newRow("three decimals") << QStringLiteral("12,345");
    QTest::newRow("ragged grouping") << QStringLiteral("1,23,456.00");
    QTest::newRow("two separators at the end") << QStringLiteral("1.234.56,7");
    QTest::newRow("too large") << QStringLiteral("999999999999999999999999");
}

void TestInputText::rejectsWhatIsNotAnAmount()
{
    QFETCH(QString, text);

    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale{QStringLiteral("sv_SE")});

    const MoneyInput parsed = parseMoneyInput(text, domain::Currency::Sek);
    QVERIFY(!parsed.ok);
    QVERIFY(!parsed.error.isEmpty());
    QVERIFY(parsed.label.isEmpty());

    QLocale::setDefault(previous);
}

void TestInputText::readsRatesAsBasisPoints()
{
    const PercentageInput whole = parsePercentageInput(QStringLiteral("4"));
    QVERIFY(whole.ok);
    QCOMPARE(whole.value.basisPoints(), 400);

    const PercentageInput half = parsePercentageInput(QStringLiteral("4,5"));
    QVERIFY(half.ok);
    QCOMPARE(half.value.basisPoints(), 450);
    QCOMPARE(half.label, formatPercentage(half.value));

    const PercentageInput precise = parsePercentageInput(QStringLiteral("3.75"));
    QVERIFY(precise.ok);
    QCOMPARE(precise.value.basisPoints(), 375);

    const PercentageInput empty = parsePercentageInput(QString{});
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());
}

void TestInputText::rejectsRatesWithTooManyDecimals()
{
    const PercentageInput parsed = parsePercentageInput(QStringLiteral("4,567"));
    QVERIFY(!parsed.ok);
    QVERIFY(!parsed.error.isEmpty());
}

void TestInputText::formatsWithTheCurrencyCode()
{
    const domain::Money amount{123456, domain::Currency::Eur};
    const QString formatted = formatMoney(amount);
    QVERIFY(formatted.contains(QStringLiteral("EUR")));
    QVERIFY(!formatted.isEmpty());
}

QTEST_APPLESS_MAIN(TestInputText)

#include "tst_input_text.moc"
