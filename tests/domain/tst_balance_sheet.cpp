// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/balance_sheet.h"

#include <QTest>
#include <array>
#include <span>
#include <stdexcept>

using namespace hinkwise::domain;

namespace {

/// Öre, like everywhere else in the domain. 1 000 000 kr == 100 000 000.
Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

std::int64_t ore(Money amount)
{
    return amount.minorUnits();
}

} // namespace

class TestBalanceSheet : public QObject {
    Q_OBJECT

private slots:
    void perBucketTotals();
    void netWorthIsAssetsMinusLiabilities();
    void liquidAssetsExcludeHousingAndPensions();
    void headlineExcludesHousingAndPension();
    void headlineExcludesEveryHousingAndPensionKind_data();
    void headlineExcludesEveryHousingAndPensionKind();
    void headlineHonoursTheIncludedFlag();
    void bucketShareSumsTo100Percent();
    void bucketShareIsZeroWithoutAssets();
    void currencyMismatchThrows();
    void emptyInputYieldsZeroes();
    void badLoanTotalSumsOnlyTheBadLoans();
};

void TestBalanceSheet::perBucketTotals()
{
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);
    const Asset equity = makeAsset("equity", "Single share", AssetKind::Equity);

    const std::array<Holding, 3> holdings{{
        {&bank, sek(100000000)},
        {&fund, sek(300000000)},
        {&equity, sek(50000000)},
    }};

    const std::span<const Debt> noDebts;
    const BalanceSheet sheet = buildBalanceSheet(holdings, noDebts);

    QCOMPARE(ore(sheet.perBucket[static_cast<std::size_t>(Bucket::Buffer)]), qint64(100000000));
    QCOMPARE(ore(sheet.perBucket[static_cast<std::size_t>(Bucket::Preserve)]), qint64(0));
    QCOMPARE(ore(sheet.perBucket[static_cast<std::size_t>(Bucket::Growth)]), qint64(300000000));
    QCOMPARE(ore(sheet.perBucket[static_cast<std::size_t>(Bucket::Play)]), qint64(50000000));
    QCOMPARE(ore(sheet.totalAssets), qint64(450000000));
}

void TestBalanceSheet::netWorthIsAssetsMinusLiabilities()
{
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Liability mortgage = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                             Percentage::fromBasisPoints(450));

    const std::array<Holding, 1> holdings{{{&bank, sek(100000000)}}};
    const std::array<Debt, 1> debts{{{&mortgage, sek(250000000)}}};

    const BalanceSheet sheet = buildBalanceSheet(holdings, debts);

    QCOMPARE(ore(sheet.totalLiabilities), qint64(250000000));
    QCOMPARE(ore(sheet.netWorth), qint64(-150000000));
}

void TestBalanceSheet::liquidAssetsExcludeHousingAndPensions()
{
    const Asset flat = makeAsset("flat", "Flat", AssetKind::RealEstate);
    const Asset pension =
        makeAsset("tjanstep", "Occupational pension", AssetKind::PensionOccupational);
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);

    const std::array<Holding, 4> holdings{{
        {&flat, sek(300000000)},
        {&pension, sek(50000000)},
        {&bank, sek(10000000)},
        {&fund, sek(20000000)},
    }};

    const std::span<const Debt> noDebts;
    const BalanceSheet sheet = buildBalanceSheet(holdings, noDebts);

    QCOMPARE(ore(sheet.totalAssets), qint64(380000000));
    QCOMPARE(ore(sheet.liquidAssets), qint64(30000000));
}

void TestBalanceSheet::headlineExcludesHousingAndPension()
{
    const Asset flat = makeAsset("flat", "Flat", AssetKind::RealEstate);
    const Asset pension = makeAsset("inkomstpension", "Pension income", AssetKind::PensionIncome);
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Liability mortgage = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                             Percentage::fromBasisPoints(450));

    const std::array<Holding, 3> holdings{{
        {&flat, sek(300000000)},
        {&pension, sek(50000000)},
        {&bank, sek(10000000)},
    }};
    const std::array<Debt, 1> debts{{{&mortgage, sek(200000000)}}};

    const BalanceSheet sheet = buildBalanceSheet(holdings, debts);

    // 3 600 000 kr of assets against a 2 000 000 kr mortgage.
    QCOMPARE(ore(sheet.netWorth), qint64(160000000));
    QCOMPARE(ore(sheet.headlineNetWorth(true)), qint64(160000000));

    // Without the flat and the pension only 100 000 kr is left to act on, and
    // the whole mortgage still has to be paid: a negative headline is correct.
    QCOMPARE(ore(sheet.headlineNetWorth(false)), qint64(-190000000));
}

void TestBalanceSheet::headlineExcludesEveryHousingAndPensionKind_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<bool>("counts");

    QTest::newRow("real estate") << int(AssetKind::RealEstate) << false;
    QTest::newRow("pension income") << int(AssetKind::PensionIncome) << false;
    QTest::newRow("occupational pension") << int(AssetKind::PensionOccupational) << false;
    QTest::newRow("premium pension") << int(AssetKind::PensionPremium) << false;
    QTest::newRow("traditional life") << int(AssetKind::TraditionalLife) << false;
    QTest::newRow("bank account") << int(AssetKind::BankAccount) << true;
    QTest::newRow("fund") << int(AssetKind::Fund) << true;
    QTest::newRow("equity") << int(AssetKind::Equity) << true;
    QTest::newRow("crypto") << int(AssetKind::Crypto) << true;
    QTest::newRow("other") << int(AssetKind::Other) << true;
}

void TestBalanceSheet::headlineExcludesEveryHousingAndPensionKind()
{
    QFETCH(int, kind);
    QFETCH(bool, counts);

    const Asset asset = makeAsset("a", "An asset", static_cast<AssetKind>(kind));
    const std::array<Holding, 1> holdings{{{&asset, sek(100000000)}}};
    const std::span<const Debt> noDebts;

    const BalanceSheet sheet = buildBalanceSheet(holdings, noDebts);

    QCOMPARE(ore(sheet.netWorth), qint64(100000000));
    const std::int64_t expected = counts ? 100000000 : 0;
    QCOMPARE(ore(sheet.headlineNetWorth(false)), expected);
}

void TestBalanceSheet::headlineHonoursTheIncludedFlag()
{
    Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    fund.includedInHeadline = false;

    const std::array<Holding, 2> holdings{{
        {&fund, sek(100000000)},
        {&bank, sek(40000000)},
    }};

    const std::span<const Debt> noDebts;
    const BalanceSheet sheet = buildBalanceSheet(holdings, noDebts);

    QCOMPARE(ore(sheet.netWorth), qint64(140000000));
    QCOMPARE(ore(sheet.headlineNetWorth(false)), qint64(40000000));
}

void TestBalanceSheet::bucketShareSumsTo100Percent()
{
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);

    const std::array<Holding, 2> holdings{{
        {&bank, sek(100000000)},
        {&fund, sek(300000000)},
    }};

    const std::span<const Debt> noDebts;
    const BalanceSheet sheet = buildBalanceSheet(holdings, noDebts);

    const int buffer = sheet.bucketShare(Bucket::Buffer).basisPoints();
    const int preserve = sheet.bucketShare(Bucket::Preserve).basisPoints();
    const int growth = sheet.bucketShare(Bucket::Growth).basisPoints();
    const int play = sheet.bucketShare(Bucket::Play).basisPoints();

    QCOMPARE(buffer, 2500); // 25 %
    QCOMPARE(growth, 7500); // 75 %
    QCOMPARE(preserve, 0);
    QCOMPARE(play, 0);
    QCOMPARE(buffer + preserve + growth + play, 10000);
}

void TestBalanceSheet::bucketShareIsZeroWithoutAssets()
{
    const std::span<const Holding> noHoldings;
    const std::span<const Debt> noDebts;

    const BalanceSheet sheet = buildBalanceSheet(noHoldings, noDebts);

    QCOMPARE(sheet.bucketShare(Bucket::Buffer).basisPoints(), 0);
    QCOMPARE(sheet.bucketShare(Bucket::Growth).basisPoints(), 0);
}

void TestBalanceSheet::currencyMismatchThrows()
{
    const Asset bank = makeAsset("bank", "Bank account", AssetKind::BankAccount);
    const Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);

    const std::array<Holding, 2> holdings{{
        {&bank, Money{100000000, Currency::Sek}},
        {&fund, Money{300000000, Currency::Eur}},
    }};

    const std::span<const Debt> noDebts;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, buildBalanceSheet(holdings, noDebts));
}

void TestBalanceSheet::emptyInputYieldsZeroes()
{
    const std::span<const Holding> noHoldings;
    const std::span<const Debt> noDebts;

    const BalanceSheet sheet = buildBalanceSheet(noHoldings, noDebts);

    QVERIFY(sheet.totalAssets.isZero());
    QVERIFY(sheet.totalLiabilities.isZero());
    QVERIFY(sheet.netWorth.isZero());
    QVERIFY(sheet.liquidAssets.isZero());
    QCOMPARE(sheet.netWorth.currency(), Currency::Sek);
    for (const Bucket bucket : allBuckets())
        QVERIFY(sheet.perBucket[static_cast<std::size_t>(bucket)].isZero());
}

void TestBalanceSheet::badLoanTotalSumsOnlyTheBadLoans()
{
    const Liability mortgage = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                             Percentage::fromBasisPoints(450));
    const Liability consumer =
        makeLiability("blankolan", "Consumer loan", LiabilityKind::ConsumerLoan,
                      Percentage::fromBasisPoints(850));
    const Liability card = makeLiability("kort", "Credit card", LiabilityKind::CreditCard,
                                         Percentage::fromBasisPoints(1995));

    const std::array<Debt, 3> debts{{
        {&mortgage, sek(200000000)},
        {&consumer, sek(8000000)},
        {&card, sek(2500000)},
    }};

    // The mortgage sits exactly on the benchmark, so it is not counted.
    const Money bad = badLoanTotal(debts, Percentage::fromBasisPoints(450));

    QCOMPARE(ore(bad), qint64(10500000));
    QCOMPARE(bad.currency(), Currency::Sek);
}

QTEST_APPLESS_MAIN(TestBalanceSheet)

#include "tst_balance_sheet.moc"
