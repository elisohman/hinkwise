// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/serialisation.h"

#include <QTest>
#include <optional>
#include <string>

using namespace hinkwise::domain;

class TestSerialisation : public QObject {
    Q_OBJECT

private slots:
    void phases_data();
    void phases();
    void currencies_data();
    void currencies();
    void buckets_data();
    void buckets();
    void assetKinds_data();
    void assetKinds();
    void liabilityKinds_data();
    void liabilityKinds();
    void trackingModes_data();
    void trackingModes();
    void rejectsUnknownKeys();
    void rejectsOutOfRangeEnums();
};

void TestSerialisation::phases_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("getting started")
        << static_cast<int>(Phase::GettingStarted) << "getting-started";
    QTest::newRow("building wealth")
        << static_cast<int>(Phase::BuildingWealth) << "building-wealth";
    QTest::newRow("preserving") << static_cast<int>(Phase::Preserving) << "preserving";
    QTest::newRow("financially independent")
        << static_cast<int>(Phase::FinanciallyIndependent) << "financially-independent";
}

void TestSerialisation::phases()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const Phase phase = static_cast<Phase>(value);
    const QString stored = QString::fromUtf8(storageKey(phase));
    QCOMPARE(stored, key);

    const std::optional<Phase> decoded = phaseFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);
}

void TestSerialisation::currencies_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("sek") << static_cast<int>(Currency::Sek) << "SEK";
    QTest::newRow("eur") << static_cast<int>(Currency::Eur) << "EUR";
    QTest::newRow("usd") << static_cast<int>(Currency::Usd) << "USD";
}

void TestSerialisation::currencies()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const Currency currency = static_cast<Currency>(value);
    const QString stored = QString::fromUtf8(storageKey(currency));
    QCOMPARE(stored, key);

    const std::optional<Currency> decoded = currencyFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);
}

void TestSerialisation::buckets_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("buffer") << static_cast<int>(Bucket::Buffer) << "buffer";
    QTest::newRow("preserve") << static_cast<int>(Bucket::Preserve) << "preserve";
    QTest::newRow("growth") << static_cast<int>(Bucket::Growth) << "growth";
    QTest::newRow("play") << static_cast<int>(Bucket::Play) << "play";
}

void TestSerialisation::buckets()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const Bucket bucket = static_cast<Bucket>(value);
    const QString stored = QString::fromUtf8(storageKey(bucket));
    QCOMPARE(stored, key);

    const std::optional<Bucket> decoded = bucketFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);
}

void TestSerialisation::assetKinds_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("bank account") << static_cast<int>(AssetKind::BankAccount) << "bank-account";
    QTest::newRow("fund") << static_cast<int>(AssetKind::Fund) << "fund";
    QTest::newRow("equity") << static_cast<int>(AssetKind::Equity) << "equity";
    QTest::newRow("crypto") << static_cast<int>(AssetKind::Crypto) << "crypto";
    QTest::newRow("real estate") << static_cast<int>(AssetKind::RealEstate) << "real-estate";
    QTest::newRow("pension income")
        << static_cast<int>(AssetKind::PensionIncome) << "pension-income";
    QTest::newRow("occupational pension")
        << static_cast<int>(AssetKind::PensionOccupational) << "pension-occupational";
    QTest::newRow("premium pension")
        << static_cast<int>(AssetKind::PensionPremium) << "pension-premium";
    QTest::newRow("traditional life")
        << static_cast<int>(AssetKind::TraditionalLife) << "traditional-life";
    QTest::newRow("other") << static_cast<int>(AssetKind::Other) << "other";
}

void TestSerialisation::assetKinds()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const AssetKind kind = static_cast<AssetKind>(value);
    const QString stored = QString::fromUtf8(storageKey(kind));
    QCOMPARE(stored, key);

    const std::optional<AssetKind> decoded = assetKindFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);
}

void TestSerialisation::liabilityKinds_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("mortgage") << static_cast<int>(LiabilityKind::Mortgage) << "mortgage";
    QTest::newRow("student loan") << static_cast<int>(LiabilityKind::StudentLoan) << "student-loan";
    QTest::newRow("consumer loan")
        << static_cast<int>(LiabilityKind::ConsumerLoan) << "consumer-loan";
    QTest::newRow("credit card") << static_cast<int>(LiabilityKind::CreditCard) << "credit-card";
    QTest::newRow("car loan") << static_cast<int>(LiabilityKind::CarLoan) << "car-loan";
    QTest::newRow("other") << static_cast<int>(LiabilityKind::Other) << "other";
}

void TestSerialisation::liabilityKinds()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const LiabilityKind kind = static_cast<LiabilityKind>(value);
    const QString stored = QString::fromUtf8(storageKey(kind));
    QCOMPARE(stored, key);

    const std::optional<LiabilityKind> decoded = liabilityKindFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);
}

void TestSerialisation::trackingModes_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("key");

    QTest::newRow("accounting") << static_cast<int>(TrackingMode::Accounting) << "accounting";
    QTest::newRow("planning") << static_cast<int>(TrackingMode::Planning) << "planning";
}

void TestSerialisation::trackingModes()
{
    QFETCH(int, value);
    QFETCH(QString, key);

    const TrackingMode mode = static_cast<TrackingMode>(value);
    const QString stored = QString::fromUtf8(storageKey(mode));
    QCOMPARE(stored, key);

    const std::optional<TrackingMode> decoded = trackingModeFromStorageKey(key.toStdString());
    QVERIFY(decoded.has_value());
    QCOMPARE(static_cast<int>(*decoded), value);

    // The label is for the interface and is allowed to read differently.
    QVERIFY(!QString::fromUtf8(labelOf(mode)).isEmpty());
    QVERIFY(QString::fromUtf8(labelOf(mode)) != stored);
}

void TestSerialisation::rejectsUnknownKeys()
{
    // Labels, spellings from a future release and empty text are all rejected
    // rather than guessed at.
    const std::string empty;
    const std::string label = "Building wealth";
    const std::string snakeCase = "building_wealth";
    const std::string lowerCaseCurrency = "sek";
    const std::string nonsense = "spaceship";

    QVERIFY(!phaseFromStorageKey(empty).has_value());
    QVERIFY(!phaseFromStorageKey(label).has_value());
    QVERIFY(!phaseFromStorageKey(snakeCase).has_value());
    QVERIFY(!currencyFromStorageKey(lowerCaseCurrency).has_value());
    QVERIFY(!currencyFromStorageKey(empty).has_value());
    QVERIFY(!assetKindFromStorageKey(nonsense).has_value());
    QVERIFY(!assetKindFromStorageKey(snakeCase).has_value());
    QVERIFY(!liabilityKindFromStorageKey(nonsense).has_value());
    QVERIFY(!bucketFromStorageKey(empty).has_value());
    QVERIFY(!bucketFromStorageKey(nonsense).has_value());
    QVERIFY(!trackingModeFromStorageKey(empty).has_value());
    QVERIFY(!trackingModeFromStorageKey(nonsense).has_value());
    // The label is not a key either: only the two spellings above are.
    QVERIFY(!trackingModeFromStorageKey(labelOf(TrackingMode::Planning)).has_value());
}

void TestSerialisation::rejectsOutOfRangeEnums()
{
    // A corrupt enum has no key; writing an empty string is what makes it fail
    // to load instead of silently becoming a valid bucket.
    const Phase phase = static_cast<Phase>(9);
    const AssetKind kind = static_cast<AssetKind>(200);
    const Bucket bucket = static_cast<Bucket>(7);

    QCOMPARE(QString::fromUtf8(storageKey(phase)), QString{});
    QCOMPARE(QString::fromUtf8(storageKey(kind)), QString{});
    QCOMPARE(QString::fromUtf8(storageKey(bucket)), QString{});
    QCOMPARE(QString::fromUtf8(storageKey(static_cast<TrackingMode>(4))), QString{});
}

QTEST_APPLESS_MAIN(TestSerialisation)

#include "tst_serialisation.moc"
