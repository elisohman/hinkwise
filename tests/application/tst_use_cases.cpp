// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/ports.h"
#include "application/result.h"
#include "application/use_cases.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/household.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/year_month.h"

#include <QTest>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace hinkwise::application;
using namespace hinkwise::domain;

namespace {

constexpr YearMonth kMonth{2026, 3};

Money sek(qint64 minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

MonthFlow flowOf(qint64 income, qint64 needs, qint64 wants, qint64 saving)
{
    return MonthFlow{sek(income), sek(needs), sek(wants), sek(saving)};
}

class FakeProfileRepository final : public IProfileRepository {
public:
    std::optional<Profile> stored;
    bool failSave = false;

    Result<std::optional<Profile>> load() const override
    {
        return Result<std::optional<Profile>>::ok(stored);
    }

    Status save(const Profile& profile) override
    {
        if (failSave)
            return Status::err("profile could not be saved");
        stored = profile;
        return Status::success();
    }
};

class FakeAssetRepository final : public IAssetRepository {
public:
    std::vector<Asset> stored;

    Result<std::vector<Asset>> all() const override
    {
        return Result<std::vector<Asset>>::ok(stored);
    }

    Status upsert(const Asset& asset) override
    {
        stored.push_back(asset);
        return Status::success();
    }

    Status remove(std::string_view id) override
    {
        std::erase_if(stored, [id](const Asset& asset) { return asset.id == id; });
        return Status::success();
    }
};

class FakeLiabilityRepository final : public ILiabilityRepository {
public:
    std::vector<Liability> stored;

    Result<std::vector<Liability>> all() const override
    {
        return Result<std::vector<Liability>>::ok(stored);
    }

    Status upsert(const Liability& liability) override
    {
        stored.push_back(liability);
        return Status::success();
    }

    Status remove(std::string_view id) override
    {
        std::erase_if(stored, [id](const Liability& liability) { return liability.id == id; });
        return Status::success();
    }
};

class FakeValuationRepository final : public IValuationRepository {
public:
    std::map<std::string, Money> latestBalance;

    Result<std::optional<Money>> balanceAt(std::string_view, YearMonth) const override
    {
        return Result<std::optional<Money>>::ok(std::nullopt);
    }

    Result<std::optional<Money>> latest(std::string_view assetId) const override
    {
        const auto found = latestBalance.find(std::string{assetId});
        if (found == latestBalance.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(found->second);
    }

    Status upsert(std::string_view assetId, YearMonth, Money balance) override
    {
        latestBalance[std::string{assetId}] = balance;
        return Status::success();
    }
};

class FakeMonthFlowRepository final : public IMonthFlowRepository {
public:
    std::map<std::int64_t, MonthFlow> stored;

    Result<std::optional<MonthFlow>> at(YearMonth month) const override
    {
        const auto found = stored.find(month.index());
        if (found == stored.end())
            return Result<std::optional<MonthFlow>>::ok(std::nullopt);
        return Result<std::optional<MonthFlow>>::ok(found->second);
    }

    Result<std::vector<MonthFlowRecord>> recent(int) const override
    {
        return Result<std::vector<MonthFlowRecord>>::ok(std::vector<MonthFlowRecord>{});
    }

    Status upsert(YearMonth month, const MonthFlow& flow) override
    {
        stored[month.index()] = flow;
        return Status::success();
    }
};

class FakePhaseHistoryRepository final : public IPhaseHistoryRepository {
public:
    std::vector<PhaseChange> entries;
    bool failAppend = false;

    Result<std::vector<PhaseChange>> all() const override
    {
        return Result<std::vector<PhaseChange>>::ok(entries);
    }

    Status append(const PhaseChange& change) override
    {
        if (failAppend)
            return Status::err("phase history could not be written");
        entries.push_back(change);
        return Status::success();
    }
};

Profile sampleProfile()
{
    Profile profile;
    profile.displayName = "Elis";
    profile.baseCurrency = Currency::Sek;
    profile.phase = Phase::BuildingWealth;
    profile.jurisdiction = swedishJurisdiction();
    return profile;
}

} // namespace

class TestUseCases : public QObject {
    Q_OBJECT

private slots:
    void recordMonthStoresTheFlow();
    void recordMonthRejectsNegativeAmounts();
    void recordValuationStoresTheBalance();
    void recordValuationRejectsInvalidInput();
    void addAndRemoveAsset();
    void addAndRemoveLiability();
    void createProfileNeedsAName();
    void createProfileStartsInSwedishJurisdiction();
    void changePhaseAppendsHistoryAndPersists();
    void changePhaseRejectsTheCurrentPhase();
    void changePhaseRejectsAnEmptyTimestamp();
    void changePhaseKeepsTheProfileWhenTheSaveFails();
};

void TestUseCases::recordMonthStoresTheFlow()
{
    FakeMonthFlowRepository flows;
    const MonthFlow flow = flowOf(5000000, 2000000, 400000, 600000);

    const Status recorded = recordMonth(flows, kMonth, flow);
    QVERIFY2(recorded.ok(), recorded.error().c_str());

    const std::size_t expectedCount = 1;
    QCOMPARE(flows.stored.size(), expectedCount);
    const MonthFlow& stored = flows.stored.at(kMonth.index());
    QCOMPARE(stored.income.minorUnits(), qint64(5000000));
    QCOMPARE(stored.needs.minorUnits(), qint64(2000000));
    QCOMPARE(stored.wants.minorUnits(), qint64(400000));
    QCOMPARE(stored.saving.minorUnits(), qint64(600000));
}

void TestUseCases::recordMonthRejectsNegativeAmounts()
{
    FakeMonthFlowRepository flows;

    const Status negativeIncome = recordMonth(flows, kMonth, flowOf(-1, 0, 0, 0));
    QVERIFY(!negativeIncome.ok());
    QVERIFY(!negativeIncome.error().empty());

    const Status negativeNeeds = recordMonth(flows, kMonth, flowOf(0, -1, 0, 0));
    QVERIFY(!negativeNeeds.ok());
    QVERIFY(!negativeNeeds.error().empty());

    const Status negativeWants = recordMonth(flows, kMonth, flowOf(0, 0, -1, 0));
    QVERIFY(!negativeWants.ok());
    QVERIFY(!negativeWants.error().empty());

    const Status negativeSaving = recordMonth(flows, kMonth, flowOf(0, 0, 0, -1));
    QVERIFY(!negativeSaving.ok());
    QVERIFY(!negativeSaving.error().empty());

    QVERIFY(flows.stored.empty());
}

void TestUseCases::recordValuationStoresTheBalance()
{
    FakeValuationRepository valuations;

    const Status recorded = recordValuation(valuations, "fund", kMonth, sek(1000000));
    QVERIFY2(recorded.ok(), recorded.error().c_str());

    const auto found = valuations.latestBalance.find("fund");
    QVERIFY(found != valuations.latestBalance.end());
    QCOMPARE(found->second.minorUnits(), qint64(1000000));
}

void TestUseCases::recordValuationRejectsInvalidInput()
{
    FakeValuationRepository valuations;

    QVERIFY(!recordValuation(valuations, "", kMonth, sek(1000000)).ok());
    QVERIFY(!recordValuation(valuations, "fund", kMonth, sek(-1)).ok());
    QVERIFY(valuations.latestBalance.empty());
}

void TestUseCases::addAndRemoveAsset()
{
    FakeAssetRepository assets;
    const Asset fund = makeAsset("fund", "Global fund", AssetKind::Fund);

    QVERIFY(addAsset(assets, fund).ok());
    const std::size_t expectedAfterAdd = 1;
    QCOMPARE(assets.stored.size(), expectedAfterAdd);

    QVERIFY(removeAsset(assets, "fund").ok());
    QVERIFY(assets.stored.empty());

    QVERIFY(!addAsset(assets, Asset{}).ok());
}

void TestUseCases::addAndRemoveLiability()
{
    FakeLiabilityRepository liabilities;
    const Liability card = makeLiability("card", "Credit card", LiabilityKind::CreditCard,
                                         Percentage::fromBasisPoints(1995));

    QVERIFY(addLiability(liabilities, card).ok());
    const std::size_t expectedAfterAdd = 1;
    QCOMPARE(liabilities.stored.size(), expectedAfterAdd);

    QVERIFY(removeLiability(liabilities, "card").ok());
    QVERIFY(liabilities.stored.empty());
}

void TestUseCases::createProfileNeedsAName()
{
    const Result<Profile> missing = createProfile("", Currency::Sek, Phase::BuildingWealth);
    QVERIFY(!missing.ok());
    QVERIFY(!missing.error().empty());
}

void TestUseCases::createProfileStartsInSwedishJurisdiction()
{
    const Result<Profile> created = createProfile("Elis", Currency::Eur, Phase::GettingStarted);
    QVERIFY2(created.ok(), created.error().c_str());

    const Profile& profile = created.value();
    QCOMPARE(QString::fromStdString(profile.displayName), QStringLiteral("Elis"));
    QCOMPARE(static_cast<int>(profile.baseCurrency), static_cast<int>(Currency::Eur));
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::GettingStarted));
    QCOMPARE(QString::fromStdString(profile.jurisdiction.id), QStringLiteral("SE"));
    QCOMPARE(profile.riskTolerance.basisPoints(), 5000);
}

void TestUseCases::changePhaseAppendsHistoryAndPersists()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    Profile profile = sampleProfile();

    const Status changed =
        changePhase(profiles, history, profile, Phase::Preserving, "2026-03-15T12:00:00Z");
    QVERIFY2(changed.ok(), changed.error().c_str());

    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::Preserving));
    QVERIFY(profiles.stored.has_value());
    QCOMPARE(static_cast<int>(profiles.stored->phase), static_cast<int>(Phase::Preserving));

    const std::size_t expectedHistory = 1;
    QCOMPARE(history.entries.size(), expectedHistory);
    QCOMPARE(QString::fromStdString(history.entries[0].changedAt),
             QStringLiteral("2026-03-15T12:00:00Z"));
    QCOMPARE(static_cast<int>(history.entries[0].phase), static_cast<int>(Phase::Preserving));
}

void TestUseCases::changePhaseRejectsTheCurrentPhase()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    Profile profile = sampleProfile();

    const Status changed =
        changePhase(profiles, history, profile, Phase::BuildingWealth, "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(history.entries.empty());
}

void TestUseCases::changePhaseRejectsAnEmptyTimestamp()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    Profile profile = sampleProfile();

    const Status changed = changePhase(profiles, history, profile, Phase::Preserving, "");
    QVERIFY(!changed.ok());
    QVERIFY(history.entries.empty());
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
}

void TestUseCases::changePhaseKeepsTheProfileWhenTheSaveFails()
{
    FakeProfileRepository profiles;
    profiles.failSave = true;
    FakePhaseHistoryRepository history;
    Profile profile = sampleProfile();

    const Status changed =
        changePhase(profiles, history, profile, Phase::Preserving, "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
}

QTEST_APPLESS_MAIN(TestUseCases)

#include "tst_use_cases.moc"
