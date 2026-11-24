// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/ports.h"
#include "domain/asset.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "infrastructure/json/backup.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTest>
#include <cstddef>
#include <string>
#include <string_view>

using namespace hinkwise;
using namespace hinkwise::infrastructure;

namespace {

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
}

/// A backup with at least one of everything, so a round trip that drops a
/// section cannot pass by accident.
json::BackupData fullyPopulated()
{
    json::BackupData data;

    data.profile.displayName = "Elis";
    data.profile.baseCurrency = domain::Currency::Sek;
    data.profile.phase = domain::Phase::Preserving;
    data.profile.riskTolerance = domain::Percentage::fromBasisPoints(4000);
    data.profile.jurisdiction = domain::swedishJurisdiction();
    // Accounting rather than the default, so a round trip that drops the field
    // cannot pass by landing on Planning anyway.
    data.profile.trackingMode = domain::TrackingMode::Accounting;

    const domain::Asset fund = domain::makeAsset("fund", "Global fund", domain::AssetKind::Fund);
    domain::Asset flat = domain::makeAsset("flat", "Flat", domain::AssetKind::RealEstate);
    flat.liquid = false;
    flat.includedInHeadline = false;
    data.assets = {fund, flat};

    data.liabilities = {domain::makeLiability("bolan", "Mortgage", domain::LiabilityKind::Mortgage,
                                              domain::Percentage::fromBasisPoints(420),
                                              domain::RepaymentStyle::Annuity, sek(1750000),
                                              sek(5000))};

    domain::PlannedExpense roof;
    roof.id = "roof";
    roof.name = "New roof";
    roof.amount = sek(25000000);
    roof.dueMonth = domain::YearMonth{2029, 6};
    data.plannedExpenses = {roof};

    json::AssetValuations valuations;
    valuations.assetId = "fund";
    valuations.points = {{domain::YearMonth{2026, 1}, sek(1000000)},
                         {domain::YearMonth{2026, 2}, sek(1050000)}};
    data.assetValuations = {valuations};

    json::LiabilityBalances balances;
    balances.liabilityId = "bolan";
    balances.points = {{domain::YearMonth{2026, 1}, sek(250000000)}};
    data.liabilityBalances = {balances};

    domain::MonthFlow flow;
    flow.income = sek(3000000);
    flow.needs = sek(1500000);
    flow.wants = sek(500000);
    flow.saving = sek(1000000);
    // January records where the saving went; February records only its total,
    // which is what a month written before the breakdown looks like.
    flow.savingSplit = domain::SavingsBreakdown{sek(400000), sek(400000), sek(200000)};
    domain::MonthFlow totalOnly;
    totalOnly.income = sek(3100000);
    totalOnly.needs = sek(1400000);
    totalOnly.wants = sek(600000);
    totalOnly.saving = sek(1100000);
    data.monthFlows = {application::MonthFlowRecord{domain::YearMonth{2026, 1}, flow},
                       application::MonthFlowRecord{domain::YearMonth{2026, 2}, totalOnly}};

    data.phaseHistory = {
        application::PhaseChange{"2026-01-31T12:00:00Z", domain::Phase::GettingStarted},
        application::PhaseChange{"2026-06-30T09:15:00Z", domain::Phase::Preserving}};

    return data;
}

QJsonObject parsedObject(std::string_view text)
{
    return QJsonDocument::fromJson(QByteArray{text.data(), static_cast<qsizetype>(text.size())})
        .object();
}

std::string serialise(const QJsonObject& object)
{
    return QJsonDocument{object}.toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

class TestBackup : public QObject {
    Q_OBJECT

private slots:
    void roundTripIsLossless();
    void largeAmountsSurviveTheRoundTrip();
    void rejectsUnknownVersion();
    void rejectsMalformedJson();
    void rejectsMissingSection();
    void rejectsMissingField();
    void rejectsUnknownEnumText();
    void rejectsWronglyTypedField();
    void acceptsAFlowWrittenBeforeTheBreakdownExisted();
    void rejectsABreakdownThatDoesNotAddUp();
    void acceptsAProfileWrittenBeforeTrackingModeExisted();
    void rejectsUnknownTrackingMode();
    void acceptsALiabilityWrittenBeforeTermsExisted();
    void rejectsUnknownRepaymentStyle();
};

void TestBackup::roundTripIsLossless()
{
    const json::BackupData original = fullyPopulated();
    const std::string text = json::toJson(original);

    const application::Result<json::BackupData> parsed = json::backupFromJson(text);
    QVERIFY2(parsed.ok(), parsed.error().c_str());

    // The strongest statement of losslessness: writing the parsed value again
    // produces the same document.
    const std::string again = json::toJson(parsed.value());
    QVERIFY(parsedObject(again) == parsedObject(text));

    const json::BackupData& data = parsed.value();
    QCOMPARE(QString::fromStdString(data.profile.displayName), QStringLiteral("Elis"));
    QCOMPARE(static_cast<int>(data.profile.baseCurrency), static_cast<int>(domain::Currency::Sek));
    QCOMPARE(static_cast<int>(data.profile.phase), static_cast<int>(domain::Phase::Preserving));
    QCOMPARE(data.profile.riskTolerance.basisPoints(), 4000);
    QCOMPARE(QString::fromStdString(data.profile.jurisdiction.id), QStringLiteral("SE"));
    QCOMPARE(data.profile.jurisdiction.safeWithdrawalRate.basisPoints(), 400);
    QCOMPARE(data.profile.jurisdiction.capitalTaxDrag.basisPoints(), 100);
    QCOMPARE(data.profile.jurisdiction.fiMultiplierDefault, 25);
    QCOMPARE(static_cast<int>(data.profile.trackingMode),
             static_cast<int>(domain::TrackingMode::Accounting));

    QCOMPARE(data.assets.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(data.assets[1].id), QStringLiteral("flat"));
    QCOMPARE(static_cast<int>(data.assets[1].kind),
             static_cast<int>(domain::AssetKind::RealEstate));
    QCOMPARE(data.assets[1].liquid, false);
    QCOMPARE(data.assets[1].includedInHeadline, false);

    QCOMPARE(data.liabilities.size(), std::size_t(1));
    QCOMPARE(static_cast<int>(data.liabilities[0].kind),
             static_cast<int>(domain::LiabilityKind::Mortgage));
    QCOMPARE(data.liabilities[0].interestRate.basisPoints(), 420);
    QCOMPARE(static_cast<int>(data.liabilities[0].style),
             static_cast<int>(domain::RepaymentStyle::Annuity));
    QCOMPARE(data.liabilities[0].regularPayment.minorUnits(), qint64(1750000));
    QCOMPARE(data.liabilities[0].monthlyFee.minorUnits(), qint64(5000));

    QCOMPARE(data.plannedExpenses.size(), std::size_t(1));
    QCOMPARE(data.plannedExpenses[0].amount.minorUnits(), qint64(25000000));
    QCOMPARE(QString::fromStdString(data.plannedExpenses[0].dueMonth.toString()),
             QStringLiteral("2029-06"));

    QCOMPARE(data.assetValuations.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(data.assetValuations[0].assetId), QStringLiteral("fund"));
    QCOMPARE(data.assetValuations[0].points.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(data.assetValuations[0].points[1].month.toString()),
             QStringLiteral("2026-02"));
    QCOMPARE(data.assetValuations[0].points[1].amount.minorUnits(), qint64(1050000));

    QCOMPARE(data.liabilityBalances.size(), std::size_t(1));
    QCOMPARE(data.liabilityBalances[0].points[0].amount.minorUnits(), qint64(250000000));

    QCOMPARE(data.monthFlows.size(), std::size_t(2));
    QCOMPARE(data.monthFlows[0].flow.income.minorUnits(), qint64(3000000));
    QCOMPARE(data.monthFlows[0].flow.saving.minorUnits(), qint64(1000000));
    QVERIFY(data.monthFlows[0].flow.savingSplit.has_value());
    QCOMPARE(data.monthFlows[0].flow.savingSplit->toBuffer.minorUnits(), qint64(400000));
    QCOMPARE(data.monthFlows[0].flow.savingSplit->toInvesting.minorUnits(), qint64(400000));
    QCOMPARE(data.monthFlows[0].flow.savingSplit->toAmortisation.minorUnits(), qint64(200000));
    QCOMPARE(static_cast<int>(data.monthFlows[0].flow.savingSplit->toBuffer.currency()),
             static_cast<int>(domain::Currency::Sek));

    // The month that recorded only its total comes back with no breakdown, and
    // writing it again leaves the document without the key.
    QVERIFY(!data.monthFlows[1].flow.savingSplit.has_value());
    QCOMPARE(data.monthFlows[1].flow.saving.minorUnits(), qint64(1100000));

    QCOMPARE(data.phaseHistory.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(data.phaseHistory[1].changedAt),
             QStringLiteral("2026-06-30T09:15:00Z"));
    QCOMPARE(static_cast<int>(data.phaseHistory[1].phase),
             static_cast<int>(domain::Phase::Preserving));
}

void TestBackup::largeAmountsSurviveTheRoundTrip()
{
    // Beyond 2^53 a double would silently round; the format stores minor units
    // as integers, so it does not.
    const qint64 huge = 9007199254740993LL;
    json::BackupData data;
    json::AssetValuations valuations;
    valuations.assetId = "fund";
    valuations.points = {{domain::YearMonth{2026, 1}, sek(huge)}};
    data.assetValuations = {valuations};

    const application::Result<json::BackupData> parsed = json::backupFromJson(json::toJson(data));
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    QCOMPARE(parsed.value().assetValuations[0].points[0].amount.minorUnits(), huge);
}

void TestBackup::rejectsUnknownVersion()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    root.insert(QStringLiteral("version"), 2);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("version 2") != std::string::npos);

    QJsonObject old = parsedObject(json::toJson(fullyPopulated()));
    old.insert(QStringLiteral("version"), 0);
    const application::Result<json::BackupData> older = json::backupFromJson(serialise(old));
    QVERIFY(!older.ok());
    QVERIFY(older.error().find("version 0") != std::string::npos);
}

void TestBackup::rejectsMalformedJson()
{
    const application::Result<json::BackupData> broken = json::backupFromJson("{ not json at all");
    QVERIFY(!broken.ok());
    QVERIFY(broken.error().find("invalid JSON") != std::string::npos);

    const application::Result<json::BackupData> notAnObject = json::backupFromJson("[1, 2, 3]");
    QVERIFY(!notAnObject.ok());
    QVERIFY(notAnObject.error().find("not a JSON object") != std::string::npos);

    const application::Result<json::BackupData> empty = json::backupFromJson("");
    QVERIFY(!empty.ok());
}

void TestBackup::rejectsMissingSection()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    root.remove(QStringLiteral("assets"));

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("assets") != std::string::npos);
}

void TestBackup::rejectsMissingField()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.remove(QStringLiteral("phase"));
    root.insert(QStringLiteral("profile"), profile);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("phase") != std::string::npos);
}

void TestBackup::rejectsUnknownEnumText()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("phase"), QStringLiteral("retired"));
    root.insert(QStringLiteral("profile"), profile);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("retired") != std::string::npos);
}

void TestBackup::rejectsWronglyTypedField()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("riskToleranceBp"), QStringLiteral("high"));
    root.insert(QStringLiteral("profile"), profile);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("riskToleranceBp") != std::string::npos);
}

void TestBackup::acceptsAFlowWrittenBeforeTheBreakdownExisted()
{
    // A backup from the release before the breakdown has no 'savingSplit' key.
    // The month is still a month: absence means the total is all that was
    // recorded, not that the file is broken.
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonArray flows = root.value(QStringLiteral("monthFlows")).toArray();
    QJsonObject january = flows.at(0).toObject();
    january.remove(QStringLiteral("savingSplit"));
    flows.replace(0, january);
    root.insert(QStringLiteral("monthFlows"), flows);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    QCOMPARE(parsed.value().monthFlows.size(), std::size_t(2));
    QCOMPARE(parsed.value().monthFlows[0].flow.saving.minorUnits(), qint64(1000000));
    QVERIFY(!parsed.value().monthFlows[0].flow.savingSplit.has_value());
}

void TestBackup::rejectsABreakdownThatDoesNotAddUp()
{
    // 4 000 + 4 000 + 1 000 kr is not the 10 000 kr saved in January. The file
    // was edited, and guessing which figure to trust would invent one.
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonArray flows = root.value(QStringLiteral("monthFlows")).toArray();
    QJsonObject january = flows.at(0).toObject();
    QJsonObject split = january.value(QStringLiteral("savingSplit")).toObject();
    QJsonObject amortisation = split.value(QStringLiteral("amortisation")).toObject();
    amortisation.insert(QStringLiteral("minor"), 100000);
    split.insert(QStringLiteral("amortisation"), amortisation);
    january.insert(QStringLiteral("savingSplit"), split);
    flows.replace(0, january);
    root.insert(QStringLiteral("monthFlows"), flows);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    // The message has to say which month is wrong.
    QVERIFY(parsed.error().find("2026-01") != std::string::npos);
    QVERIFY(parsed.error().find("breakdown") != std::string::npos);
}

/// The tracking mode was added after the first backup format was written, so a
/// file without the key is still a file this build reads. Absence means
/// Planning: the mode an existing household was already keeping its record in.
void TestBackup::acceptsAProfileWrittenBeforeTrackingModeExisted()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.remove(QStringLiteral("trackingMode"));
    root.insert(QStringLiteral("profile"), profile);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    QCOMPARE(static_cast<int>(parsed.value().profile.trackingMode),
             static_cast<int>(domain::TrackingMode::Planning));
}

/// A key that is there and says something unrecognised is a different matter:
/// the file claims to name a mode, and guessing which one would be inventing a
/// setting the household never chose.
void TestBackup::rejectsUnknownTrackingMode()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("trackingMode"), QStringLiteral("monthly"));
    root.insert(QStringLiteral("profile"), profile);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("monthly") != std::string::npos);
    QVERIFY(parsed.error().find("trackingMode") != std::string::npos);
}

/// The repayment terms arrived after the first backup format, so a liability
/// without them is still a liability: fixed amortisation with a zero payment,
/// which is the model's "no terms recorded yet".
void TestBackup::acceptsALiabilityWrittenBeforeTermsExisted()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonArray liabilities = root.value(QStringLiteral("liabilities")).toArray();
    QJsonObject mortgage = liabilities.at(0).toObject();
    mortgage.remove(QStringLiteral("repaymentStyle"));
    mortgage.remove(QStringLiteral("regularPayment"));
    mortgage.remove(QStringLiteral("monthlyFee"));
    liabilities.replace(0, mortgage);
    root.insert(QStringLiteral("liabilities"), liabilities);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    QCOMPARE(static_cast<int>(parsed.value().liabilities[0].style),
             static_cast<int>(domain::RepaymentStyle::FixedAmortisation));
    QVERIFY(parsed.value().liabilities[0].regularPayment.isZero());
    QVERIFY(parsed.value().liabilities[0].monthlyFee.isZero());
}

/// A style that is there and says something unrecognised is a different
/// matter: guessing which repayment the file meant would invent terms the
/// household never recorded.
void TestBackup::rejectsUnknownRepaymentStyle()
{
    QJsonObject root = parsedObject(json::toJson(fullyPopulated()));
    QJsonArray liabilities = root.value(QStringLiteral("liabilities")).toArray();
    QJsonObject mortgage = liabilities.at(0).toObject();
    mortgage.insert(QStringLiteral("repaymentStyle"), QStringLiteral("interest-only"));
    liabilities.replace(0, mortgage);
    root.insert(QStringLiteral("liabilities"), liabilities);

    const application::Result<json::BackupData> parsed = json::backupFromJson(serialise(root));
    QVERIFY(!parsed.ok());
    QVERIFY(parsed.error().find("interest-only") != std::string::npos);
    QVERIFY(parsed.error().find("repaymentStyle") != std::string::npos);
}

QTEST_APPLESS_MAIN(TestBackup)

#include "tst_backup.moc"
