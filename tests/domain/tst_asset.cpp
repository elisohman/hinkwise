// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/asset.h"

#include <QTest>
#include <stdexcept>

using namespace hinkwise::domain;

/// The defaults are the model's placement of each kind of wealth in a bucket.
/// If these drift, the app stops telling the user what the model tells them.
class TestAsset : public QObject {
    Q_OBJECT

private slots:
    void defaultBucketAndLiquidity_data();
    void defaultBucketAndLiquidity();
    void makeAssetRejectsEmptyId();
    void makeAssetRejectsEmptyName();
    void makeAssetAppliesTheDefaults();
    void withBucketOverridesTheDefault();
};

void TestAsset::defaultBucketAndLiquidity_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("bucket");
    QTest::addColumn<bool>("liquid");
    QTest::addColumn<QString>("label");

    QTest::newRow("bank account") << int(AssetKind::BankAccount) << int(Bucket::Buffer) << true
                                  << QStringLiteral("Bank account");
    QTest::newRow("fund") << int(AssetKind::Fund) << int(Bucket::Growth) << true
                          << QStringLiteral("Fund");
    QTest::newRow("equity") << int(AssetKind::Equity) << int(Bucket::Play) << true
                            << QStringLiteral("Equity");
    QTest::newRow("crypto") << int(AssetKind::Crypto) << int(Bucket::Play) << true
                            << QStringLiteral("Crypto");
    QTest::newRow("real estate") << int(AssetKind::RealEstate) << int(Bucket::Preserve) << false
                                 << QStringLiteral("Real estate");
    QTest::newRow("pension income") << int(AssetKind::PensionIncome) << int(Bucket::Preserve)
                                    << false << QStringLiteral("Pension income");
    QTest::newRow("occupational pension")
        << int(AssetKind::PensionOccupational) << int(Bucket::Growth) << false
        << QStringLiteral("Occupational pension");
    QTest::newRow("premium pension") << int(AssetKind::PensionPremium) << int(Bucket::Growth)
                                     << false << QStringLiteral("Premium pension");
    QTest::newRow("traditional life") << int(AssetKind::TraditionalLife) << int(Bucket::Preserve)
                                      << false << QStringLiteral("Traditional life insurance");
    QTest::newRow("other") << int(AssetKind::Other) << int(Bucket::Preserve) << true
                           << QStringLiteral("Other");
}

void TestAsset::defaultBucketAndLiquidity()
{
    QFETCH(int, kind);
    QFETCH(int, bucket);
    QFETCH(bool, liquid);
    QFETCH(QString, label);

    const AssetKindFacts& facts = factsOf(static_cast<AssetKind>(kind));

    QCOMPARE(static_cast<int>(facts.defaultBucket), bucket);
    QCOMPARE(facts.defaultLiquid, liquid);
    QCOMPARE(QString::fromUtf8(facts.label), label);
}

void TestAsset::makeAssetRejectsEmptyId()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeAsset(std::string{}, "Aurora", AssetKind::BankAccount));
}

void TestAsset::makeAssetRejectsEmptyName()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeAsset("aurora", std::string{}, AssetKind::BankAccount));
}

void TestAsset::makeAssetAppliesTheDefaults()
{
    const Asset fund = makeAsset("aurora", "Aurora fund", AssetKind::Fund);

    QCOMPARE(QString::fromStdString(fund.id), QStringLiteral("aurora"));
    QCOMPARE(QString::fromStdString(fund.name), QStringLiteral("Aurora fund"));
    QCOMPARE(fund.kind, AssetKind::Fund);
    QCOMPARE(fund.bucket, Bucket::Growth);
    QVERIFY(fund.liquid);
    QVERIFY(fund.includedInHeadline);
}

void TestAsset::withBucketOverridesTheDefault()
{
    const Asset fund = makeAsset("aurora", "Aurora fund", AssetKind::Fund);
    QCOMPARE(fund.bucket, Bucket::Growth);

    const Asset overridden = withBucket(fund, Bucket::Preserve);
    QCOMPARE(overridden.bucket, Bucket::Preserve);
    // Only the bucket moves; the holding itself is unchanged.
    QCOMPARE(QString::fromStdString(overridden.id), QStringLiteral("aurora"));
    QCOMPARE(overridden.kind, AssetKind::Fund);
    QVERIFY(overridden.liquid);
}

QTEST_APPLESS_MAIN(TestAsset)

#include "tst_asset.moc"
