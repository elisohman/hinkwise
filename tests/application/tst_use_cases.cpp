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
#include <functional>
#include <map>
#include <memory>
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
        const auto existing =
            std::find_if(stored.begin(), stored.end(),
                         [&liability](const Liability& item) { return item.id == liability.id; });
        if (existing == stored.end())
            stored.push_back(liability);
        else
            *existing = liability;
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
    std::map<std::string, std::map<YearMonth, Money>> stored;

    Result<std::optional<Money>> balanceAt(std::string_view assetId, YearMonth month) const override
    {
        const auto asset = stored.find(std::string{assetId});
        if (asset == stored.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        const auto point = asset->second.find(month);
        if (point == asset->second.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(point->second);
    }

    Result<std::optional<Money>> latest(std::string_view assetId) const override
    {
        const auto asset = stored.find(std::string{assetId});
        if (asset == stored.end() || asset->second.empty())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(asset->second.rbegin()->second);
    }

    Result<std::vector<MoneyPoint>> history(std::string_view assetId) const override
    {
        std::vector<MoneyPoint> points;
        const auto asset = stored.find(std::string{assetId});
        if (asset != stored.end()) {
            for (const auto& point : asset->second)
                points.push_back(MoneyPoint{point.first, point.second});
        }
        return Result<std::vector<MoneyPoint>>::ok(std::move(points));
    }

    Result<std::optional<YearMonth>> mostRecentMonth() const override
    {
        std::optional<YearMonth> newest;
        for (const auto& asset : stored) {
            if (!asset.second.empty() && (!newest || *newest < asset.second.rbegin()->first))
                newest = asset.second.rbegin()->first;
        }
        return Result<std::optional<YearMonth>>::ok(newest);
    }

    Status upsert(std::string_view assetId, YearMonth month, Money balance) override
    {
        stored[std::string{assetId}][month] = balance;
        return Status::success();
    }
};

/// Debt balances keyed by debt and month. `liabilities` stands in for the
/// foreign key the real table enforces: a balance for a debt that does not
/// exist is refused rather than stored and orphaned.
class FakeDebtBalanceRepository final : public IDebtBalanceRepository {
public:
    std::vector<std::string> liabilities{"bolan"};
    std::map<std::string, std::map<YearMonth, Money>> stored;

    Result<std::optional<Money>> balanceAt(std::string_view liabilityId,
                                           YearMonth month) const override
    {
        const auto debt = stored.find(std::string{liabilityId});
        if (debt == stored.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        const auto point = debt->second.find(month);
        if (point == debt->second.end())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(point->second);
    }

    Result<std::optional<Money>> latest(std::string_view liabilityId) const override
    {
        const auto debt = stored.find(std::string{liabilityId});
        if (debt == stored.end() || debt->second.empty())
            return Result<std::optional<Money>>::ok(std::nullopt);
        return Result<std::optional<Money>>::ok(debt->second.rbegin()->second);
    }

    Result<std::vector<MoneyPoint>> history(std::string_view liabilityId) const override
    {
        std::vector<MoneyPoint> points;
        const auto debt = stored.find(std::string{liabilityId});
        if (debt != stored.end()) {
            for (const auto& point : debt->second)
                points.push_back(MoneyPoint{point.first, point.second});
        }
        return Result<std::vector<MoneyPoint>>::ok(std::move(points));
    }

    Result<std::optional<YearMonth>> mostRecentMonth() const override
    {
        std::optional<YearMonth> newest;
        for (const auto& debt : stored) {
            if (!debt.second.empty() && (!newest || *newest < debt.second.rbegin()->first))
                newest = debt.second.rbegin()->first;
        }
        return Result<std::optional<YearMonth>>::ok(newest);
    }

    Status upsert(std::string_view liabilityId, YearMonth month, Money balance) override
    {
        const std::string id{liabilityId};
        if (std::find(liabilities.begin(), liabilities.end(), id) == liabilities.end())
            return Status::err("debt '" + id + "' does not exist");

        stored[id][month] = balance;
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

/// A stand-in for the database's transaction.
///
/// The fakes write straight into their own containers, so a rollback has to put
/// them back: the factory is told how, and does it both when a transaction is
/// dropped without a commit and when its commit fails - the two ways SQLite ends
/// a transaction without storing it.
class FakeTransactionFactory final : public ITransactionFactory {
public:
    /// Restores the repositories the transaction covered. Empty when the test
    /// does not need undoing.
    std::function<void()> restore;
    bool failBegin = false;
    bool failCommit = false;
    int commits = 0;
    int rollbacks = 0;

    class Boundary final : public Transaction {
    public:
        explicit Boundary(FakeTransactionFactory& owner)
            : m_owner(owner)
        {
        }

        ~Boundary() override
        {
            if (m_ended)
                return;
            m_owner.rollBack();
        }

        Status commit() override
        {
            m_ended = true;
            if (m_owner.failCommit) {
                m_owner.rollBack();
                return Status::err("the boundary could not be committed");
            }
            ++m_owner.commits;
            return Status::success();
        }

    private:
        FakeTransactionFactory& m_owner;
        bool m_ended = false;
    };

    Result<std::unique_ptr<Transaction>> begin() override
    {
        if (failBegin)
            return Result<std::unique_ptr<Transaction>>::err("no boundary");
        return Result<std::unique_ptr<Transaction>>::ok(std::make_unique<Boundary>(*this));
    }

    void rollBack()
    {
        ++rollbacks;
        if (restore)
            restore();
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
    void recordMonthAcceptsAPastMonth();
    void recordMonthRejectsNegativeAmounts();
    void recordValuationStoresTheBalance();
    void recordValuationAcceptsAPastMonth();
    void recordValuationRejectsInvalidInput();
    void recordDebtBalanceStoresAndCorrectsTheMonth();
    void recordDebtBalanceRejectsInvalidInput();
    void addAndRemoveAsset();
    void addAndRemoveLiability();
    void updateLiabilityChangesTheTermsAndKeepsTheLoan();
    void updateLiabilityRejectsInvalidTerms();
    void createProfileNeedsAName();
    void createProfileStartsInSwedishJurisdiction();
    void changePhaseAppendsHistoryAndPersists();
    void changePhaseRejectsTheCurrentPhase();
    void changePhaseRejectsAnEmptyTimestamp();
    void changePhaseRollsBackBothWritesWhenTheSaveFails();
    void changePhaseReportsAFailedCommitAndStoresNothing();
    void changePhaseReportsWhenNoBoundaryCanBeOpened();
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

void TestUseCases::recordMonthAcceptsAPastMonth()
{
    FakeMonthFlowRepository flows;
    const YearMonth sixMonthsAgo = kMonth.plusMonths(-6);

    QVERIFY2(recordMonth(flows, sixMonthsAgo, flowOf(4000000, 1600000, 400000, 500000)).ok(),
             "backfill");
    QVERIFY2(recordMonth(flows, kMonth, flowOf(5000000, 2000000, 400000, 600000)).ok(),
             "this month");

    // The older month is kept, and the newer one is what at() finds.
    const std::size_t expectedMonths = 2;
    QCOMPARE(flows.stored.size(), expectedMonths);
    QCOMPARE(flows.stored.at(sixMonthsAgo.index()).income.minorUnits(), qint64(4000000));
    QCOMPARE(flows.stored.at(kMonth.index()).income.minorUnits(), qint64(5000000));
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

    const auto found = valuations.stored.find("fund");
    QVERIFY(found != valuations.stored.end());
    QCOMPARE(found->second.at(kMonth).minorUnits(), qint64(1000000));
}

/// Backfilling is not a special case: the month is a key, so a month in the past
/// is recorded like any other, and the latest valuation stays the latest one.
void TestUseCases::recordValuationAcceptsAPastMonth()
{
    FakeValuationRepository valuations;
    const YearMonth sixMonthsAgo = kMonth.plusMonths(-6);

    QVERIFY2(recordValuation(valuations, "fund", sixMonthsAgo, sek(900000)).ok(), "backfill");
    QVERIFY2(recordValuation(valuations, "fund", kMonth, sek(1000000)).ok(), "this month");

    const Result<std::vector<MoneyPoint>> history = valuations.history("fund");
    QVERIFY2(history.ok(), history.error().c_str());
    const std::size_t expectedPoints = 2;
    QCOMPARE(history.value().size(), expectedPoints);
    const YearMonth oldestMonth = history.value().front().month;
    QCOMPARE(QString::fromStdString(oldestMonth.toString()),
             QString::fromStdString(sixMonthsAgo.toString()));

    const Result<std::optional<Money>> latest = valuations.latest("fund");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QCOMPARE(latest.value()->minorUnits(), qint64(1000000));
}

void TestUseCases::recordDebtBalanceStoresAndCorrectsTheMonth()
{
    FakeDebtBalanceRepository debtBalances;

    const Status recorded = recordDebtBalance(debtBalances, "bolan", kMonth, sek(150000000));
    QVERIFY2(recorded.ok(), recorded.error().c_str());

    // A second entry for the same month is a correction, not a second balance.
    const Status corrected = recordDebtBalance(debtBalances, "bolan", kMonth, sek(149000000));
    QVERIFY2(corrected.ok(), corrected.error().c_str());

    const Result<std::vector<MoneyPoint>> history = debtBalances.history("bolan");
    QVERIFY2(history.ok(), history.error().c_str());
    const std::size_t expectedPoints = 1;
    QCOMPARE(history.value().size(), expectedPoints);
    QCOMPARE(history.value().front().amount.minorUnits(), qint64(149000000));
}

void TestUseCases::recordDebtBalanceRejectsInvalidInput()
{
    FakeDebtBalanceRepository debtBalances;

    // A debt that does not exist is refused, which is what the foreign key in
    // the real table says too.
    const Status unknown = recordDebtBalance(debtBalances, "card", kMonth, sek(2000000));
    QVERIFY(!unknown.ok());
    QVERIFY(!unknown.error().empty());

    const Status missing = recordDebtBalance(debtBalances, "", kMonth, sek(2000000));
    QVERIFY(!missing.ok());

    const Status negative = recordDebtBalance(debtBalances, "bolan", kMonth, sek(-1));
    QVERIFY(!negative.ok());

    QVERIFY(debtBalances.stored.empty());
}

void TestUseCases::recordValuationRejectsInvalidInput()
{
    FakeValuationRepository valuations;

    QVERIFY(!recordValuation(valuations, "", kMonth, sek(1000000)).ok());
    QVERIFY(!recordValuation(valuations, "fund", kMonth, sek(-1)).ok());
    QVERIFY(valuations.stored.empty());
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

void TestUseCases::updateLiabilityChangesTheTermsAndKeepsTheLoan()
{
    FakeLiabilityRepository liabilities;
    const Liability mortgage = makeLiability(
        "bolan", "Mortgage", LiabilityKind::Mortgage, Percentage::fromBasisPoints(265),
        RepaymentStyle::FixedAmortisation, sek(300000), sek(0));
    QVERIFY(addLiability(liabilities, mortgage).ok());

    Liability repriced = mortgage;
    repriced.interestRate = Percentage::fromBasisPoints(310);
    repriced.style = RepaymentStyle::Annuity;
    repriced.regularPayment = sek(350000);
    repriced.monthlyFee = sek(5000);
    QVERIFY(updateLiability(liabilities, repriced).ok());

    // The loan is corrected, not replaced by a second one: the id is what every
    // recorded balance hangs on.
    const std::size_t expectedCount = 1;
    QCOMPARE(liabilities.stored.size(), expectedCount);
    QCOMPARE(QString::fromStdString(liabilities.stored.front().id), QStringLiteral("bolan"));
    QCOMPARE(liabilities.stored.front().interestRate.basisPoints(), 310);
    QCOMPARE(static_cast<int>(liabilities.stored.front().style),
             static_cast<int>(RepaymentStyle::Annuity));
    QCOMPARE(liabilities.stored.front().regularPayment.minorUnits(), qint64(350000));
    QCOMPARE(liabilities.stored.front().monthlyFee.minorUnits(), qint64(5000));
}

void TestUseCases::updateLiabilityRejectsInvalidTerms()
{
    FakeLiabilityRepository liabilities;
    Liability loan = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                   Percentage::fromBasisPoints(265), RepaymentStyle::Annuity,
                                   sek(300000), sek(5000));

    loan.regularPayment = sek(-1);
    QVERIFY(!updateLiability(liabilities, loan).ok());

    loan = makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage, Percentage{});
    loan.monthlyFee = sek(5000);
    QVERIFY(!updateLiability(liabilities, loan).ok());

    // A term is a count of months left: below zero is not a loan with no time
    // left but a broken figure, and it must not reach the store.
    loan = makeLiability("csn", "Student loan", LiabilityKind::StudentLoan,
                         Percentage::fromBasisPoints(150), RepaymentStyle::Annuity, sek(150000));
    loan.termMonths = -1;
    QVERIFY(!updateLiability(liabilities, loan).ok());

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
    FakeTransactionFactory transactions;
    Profile profile = sampleProfile();

    const Status changed = changePhase(profiles, history, transactions, profile, Phase::Preserving,
                                       "2026-03-15T12:00:00Z");
    QVERIFY2(changed.ok(), changed.error().c_str());

    // Both writes went through one boundary, which was committed once.
    QCOMPARE(transactions.commits, 1);
    QCOMPARE(transactions.rollbacks, 0);

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
    FakeTransactionFactory transactions;
    Profile profile = sampleProfile();

    const Status changed = changePhase(profiles, history, transactions, profile,
                                       Phase::BuildingWealth, "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(history.entries.empty());
}

void TestUseCases::changePhaseRejectsAnEmptyTimestamp()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    FakeTransactionFactory transactions;
    Profile profile = sampleProfile();

    const Status changed =
        changePhase(profiles, history, transactions, profile, Phase::Preserving, "");
    QVERIFY(!changed.ok());
    QVERIFY(history.entries.empty());
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
}

/// The history entry is written first, so a failed save used to leave a change on
/// record that the profile never got. The boundary takes it back.
void TestUseCases::changePhaseRollsBackBothWritesWhenTheSaveFails()
{
    FakeProfileRepository profiles;
    profiles.failSave = true;
    FakePhaseHistoryRepository history;
    FakeTransactionFactory transactions;

    const std::optional<Profile> profileBefore = profiles.stored;
    const std::vector<PhaseChange> historyBefore = history.entries;
    transactions.restore = [&profiles, &history, profileBefore, historyBefore] {
        profiles.stored = profileBefore;
        history.entries = historyBefore;
    };

    Profile profile = sampleProfile();
    const Status changed = changePhase(profiles, history, transactions, profile, Phase::Preserving,
                                       "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(!changed.error().empty());
    QCOMPARE(transactions.commits, 0);
    QCOMPARE(transactions.rollbacks, 1);

    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
    QVERIFY(history.entries.empty());
}

/// A commit that fails stores nothing, and the caller reads the error the
/// boundary reported rather than a success.
void TestUseCases::changePhaseReportsAFailedCommitAndStoresNothing()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    FakeTransactionFactory transactions;
    transactions.failCommit = true;

    const std::optional<Profile> profileBefore = profiles.stored;
    const std::vector<PhaseChange> historyBefore = history.entries;
    transactions.restore = [&profiles, &history, profileBefore, historyBefore] {
        profiles.stored = profileBefore;
        history.entries = historyBefore;
    };

    Profile profile = sampleProfile();
    const Status changed = changePhase(profiles, history, transactions, profile, Phase::Preserving,
                                       "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(!changed.error().empty());
    QCOMPARE(transactions.commits, 0);

    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
    QVERIFY(!profiles.stored.has_value());
    QVERIFY(history.entries.empty());
}

void TestUseCases::changePhaseReportsWhenNoBoundaryCanBeOpened()
{
    FakeProfileRepository profiles;
    FakePhaseHistoryRepository history;
    FakeTransactionFactory transactions;
    transactions.failBegin = true;

    Profile profile = sampleProfile();
    const Status changed = changePhase(profiles, history, transactions, profile, Phase::Preserving,
                                       "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(!changed.error().empty());

    // Nothing is written without a boundary to write it in.
    QVERIFY(!profiles.stored.has_value());
    QVERIFY(history.entries.empty());
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(Phase::BuildingWealth));
}

QTEST_APPLESS_MAIN(TestUseCases)

#include "tst_use_cases.moc"
