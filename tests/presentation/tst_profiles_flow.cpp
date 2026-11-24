// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/dashboard.h"
#include "application/examples.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/insight.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "infrastructure/json/backup.h"
#include "presentation/app_controller.h"
#include "presentation/profiles_view_model.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::domain;
using namespace hinkwise::presentation;

namespace {

std::int64_t minorOf(const Money& money)
{
    return money.minorUnits();
}

/// The kronor a story states, as the minor units the model stores.
std::int64_t kronor(std::int64_t whole)
{
    return whole * 100;
}

Money bucketBalance(const application::DashboardSnapshot& snapshot, Bucket bucket)
{
    return snapshot.facts.sheet.perBucket[static_cast<std::size_t>(bucket)];
}

/// Every insight that asks the household to act. The examples are built to
/// have none: a warning that contradicts the story is worse than no example.
std::vector<std::string> urgentInsightIds(const application::DashboardSnapshot& snapshot)
{
    std::vector<std::string> ids;
    for (const Insight& insight : snapshot.insights) {
        if (insight.severity == Severity::Critical || insight.severity == Severity::Warning)
            ids.push_back(insight.id);
    }
    return ids;
}

} // namespace

/// Profiles end to end against a real database in a temporary directory: an
/// example opens as the household the story describes, switching replaces every
/// figure instead of leaving the old one on screen, a switch is refused while a
/// page holds unsaved work, and a duplicate is independent and no longer an
/// example.
class TestProfilesFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void theBuildingWealthExampleIsAWholeHousehold();
    void thePreservingExampleTellsTheLaterStory();
    void aNewProfileIsEmptyAndOnboardingFillsIt();
    void switchingReplacesEveryFigure();
    void aSwitchIsRefusedWhileWorkIsUnsaved();
    void duplicatingAnExampleMakesAnIndependentCopy();
    void deletingRemovesTheProfileAndItsRowsButNeverTheLast();
    void theBackupCarriesTheExampleFlag();

private:
    std::unique_ptr<QTemporaryDir> m_dataDirectory;
    std::unique_ptr<AppController> m_controller;
};

void TestProfilesFlow::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise-profiles-test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Amounts are formatted with the default locale, so pin it: the test has to
    // read the same text on every machine.
    QLocale::setDefault(QLocale{QStringLiteral("sv_SE")});
}

void TestProfilesFlow::init()
{
    m_dataDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dataDirectory->isValid());
    QVERIFY(qputenv("XDG_DATA_HOME", m_dataDirectory->path().toUtf8()));

    m_controller = std::make_unique<AppController>(nullptr);
    QVERIFY(m_controller->initialise());
    QCOMPARE(m_controller->lastError(), QString{});
}

void TestProfilesFlow::cleanup()
{
    m_controller.reset();
    m_dataDirectory.reset();
}

void TestProfilesFlow::theBuildingWealthExampleIsAWholeHousehold()
{
    const application::Result<Profile> created =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase), static_cast<int>(Phase::BuildingWealth));
    QCOMPARE(QString::fromStdString(created.value().displayName),
             QStringLiteral("Example: building wealth"));
    QCOMPARE(m_controller->activeProfileId(), created.value().id);

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QVERIFY(snapshot.profile.isExample);
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Buffer)), kronor(80000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Preserve)), kronor(3100000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Growth)), kronor(350000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Play)), kronor(25000));

    // 15 000 needs and 9 000 wants are 24 000 a month, which is what every
    // target in the model is a multiple of.
    QCOMPARE(minorOf(snapshot.metrics.monthlyExpenses), kronor(24000));
    QCOMPARE(minorOf(snapshot.metrics.annualExpenses), kronor(288000));

    const std::vector<Liability> liabilities = m_controller->liabilities();
    QCOMPARE(liabilities.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(liabilities.front().name), QStringLiteral("Mortgage"));
    QCOMPARE(liabilities.front().interestRate.partsPer100k(), 2650);
    QCOMPARE(minorOf(liabilities.front().regularPayment), kronor(3000));

    const std::optional<Money> mortgage = m_controller->latestDebtBalance(liabilities.front().id);
    QVERIFY(mortgage.has_value());
    QCOMPARE(minorOf(*mortgage), kronor(1800000));

    // Twelve months of cashflow, ending with the month the balances belong to.
    const std::vector<application::MonthFlowRecord> flows = m_controller->recentFlows(24);
    QCOMPARE(flows.size(), std::size_t{12});
    QCOMPARE(flows.front().month, AppController::currentMonth());
    QCOMPARE(minorOf(flows.front().flow.income), kronor(30000));
    QVERIFY(flows.front().flow.savingSplit.has_value());
    QCOMPARE(minorOf(flows.front().flow.savingSplit->toAmortisation), kronor(3000));

    // Every account is valued in the current month, so the dashboard does not
    // greet the household with a staleness warning on its first screen.
    for (const Asset& asset : m_controller->assets()) {
        const std::optional<Money> value =
            m_controller->assetBalanceAt(asset.id, AppController::currentMonth());
        QVERIFY2(value.has_value(), asset.name.c_str());
    }

    const std::vector<std::string> urgent = urgentInsightIds(snapshot);
    QVERIFY2(urgent.empty(),
             urgent.empty() ? "an example fired an urgent insight" : urgent.front().c_str());
}

void TestProfilesFlow::thePreservingExampleTellsTheLaterStory()
{
    const application::Result<Profile> created =
        m_controller->createExampleProfile(application::ExampleProfile::Preserving);
    QVERIFY2(created.ok(), created.error().c_str());

    QVERIFY(created.value().isExample);
    QCOMPARE(static_cast<int>(created.value().phase), static_cast<int>(Phase::Preserving));

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Buffer)), kronor(900000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Preserve)), kronor(7200000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Growth)), kronor(2000000));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Play)), kronor(25000));

    // The mortgage is gone, so there is no debt at all.
    QVERIFY(m_controller->liabilities().empty());
    QVERIFY(snapshot.debts.empty());

    QCOMPARE(minorOf(snapshot.metrics.monthlyExpenses), kronor(24000));
    QCOMPARE(minorOf(snapshot.metrics.annualExpenses), kronor(288000));

    const std::vector<std::string> urgent = urgentInsightIds(snapshot);
    QVERIFY2(urgent.empty(),
             urgent.empty() ? "an example fired an urgent insight" : urgent.front().c_str());
}

void TestProfilesFlow::aNewProfileIsEmptyAndOnboardingFillsIt()
{
    const application::Result<Profile> example =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(example.ok(), example.error().c_str());

    const application::Result<Profile> empty = m_controller->createEmptyProfile();
    QVERIFY2(empty.ok(), empty.error().c_str());
    QCOMPARE(m_controller->activeProfileId(), empty.value().id);
    QVERIFY(!empty.value().isExample);
    QCOMPARE(QString::fromStdString(empty.value().displayName), QStringLiteral("Household"));
    QVERIFY(m_controller->assets().empty());
    QVERIFY(m_controller->recentFlows(1).empty());

    // A second empty profile is named so the list can tell them apart.
    const application::Result<Profile> secondEmpty = m_controller->createEmptyProfile();
    QVERIFY2(secondEmpty.ok(), secondEmpty.error().c_str());
    QCOMPARE(QString::fromStdString(secondEmpty.value().displayName),
             QStringLiteral("Household 2"));

    // Onboarding fills the profile it finds instead of writing a third one, and
    // it keeps the name settings gave it.
    const application::Status onboarded = m_controller->createHousehold(
        Currency::Sek, Phase::GettingStarted, TrackingMode::Accounting);
    QVERIFY2(onboarded.ok(), onboarded.error().c_str());
    QCOMPARE(m_controller->profiles().size(), std::size_t{3});

    const std::optional<Profile> active = m_controller->profile();
    QVERIFY(active.has_value());
    QCOMPARE(active->id, secondEmpty.value().id);
    QCOMPARE(QString::fromStdString(active->displayName), QStringLiteral("Household 2"));
    QCOMPARE(static_cast<int>(active->trackingMode), static_cast<int>(TrackingMode::Accounting));
}

void TestProfilesFlow::switchingReplacesEveryFigure()
{
    const application::Result<Profile> building =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(building.ok(), building.error().c_str());
    const application::Result<Profile> preserving =
        m_controller->createExampleProfile(application::ExampleProfile::Preserving);
    QVERIFY2(preserving.ok(), preserving.error().c_str());

    QCOMPARE(m_controller->profiles().size(), std::size_t{2});
    QCOMPARE(m_controller->activeProfileId(), preserving.value().id);

    ProfilesViewModel model;
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.activeName(), QStringLiteral("Example: preserving"));
    QVERIFY(model.activeIsExample());

    // A switch through the model is what the rail's switcher does.
    QCOMPARE(model.switchTo(static_cast<int>(building.value().id)), QString{});
    QCOMPARE(m_controller->activeProfileId(), building.value().id);
    QCOMPARE(model.activeName(), QStringLiteral("Example: building wealth"));

    // Nothing of the household that was open before survives: the figures, the
    // debts and the profile row all say the new one.
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(QString::fromStdString(snapshot.profile.displayName),
             QStringLiteral("Example: building wealth"));
    QCOMPARE(minorOf(bucketBalance(snapshot, Bucket::Growth)), kronor(350000));
    QCOMPARE(m_controller->liabilities().size(), std::size_t{1});

    // And back again, without a restart in between.
    QCOMPARE(model.switchTo(static_cast<int>(preserving.value().id)), QString{});
    QCOMPARE(minorOf(bucketBalance(m_controller->dashboardSnapshot(), Bucket::Growth)),
             kronor(2000000));
    QVERIFY(m_controller->liabilities().empty());
}

void TestProfilesFlow::aSwitchIsRefusedWhileWorkIsUnsaved()
{
    const application::Result<Profile> first =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(first.ok(), first.error().c_str());
    const application::Result<Profile> second =
        m_controller->createExampleProfile(application::ExampleProfile::Preserving);
    QVERIFY2(second.ok(), second.error().c_str());

    ProfilesViewModel model;

    model.setSettingsDirty(true);
    const QString settingsRefusal = model.switchTo(static_cast<int>(first.value().id));
    QVERIFY(settingsRefusal.contains(QStringLiteral("Settings")));
    QCOMPARE(model.switchError(), settingsRefusal);
    // Nothing was switched and nothing was written.
    QCOMPARE(m_controller->activeProfileId(), second.value().id);

    model.setSettingsDirty(false);
    model.setCashflowDirty(true);
    const QString cashflowRefusal = model.switchTo(static_cast<int>(first.value().id));
    QVERIFY(cashflowRefusal.contains(QStringLiteral("Cashflow")));
    QCOMPARE(m_controller->activeProfileId(), second.value().id);

    model.setCashflowDirty(false);
    model.setUpdateFlowOpen(true);
    const QString flowRefusal = model.switchTo(static_cast<int>(first.value().id));
    QVERIFY(flowRefusal.contains(QStringLiteral("update flow")));
    QCOMPARE(m_controller->activeProfileId(), second.value().id);

    // With the work resolved the same switch goes through, and the refusal that
    // was shown a moment ago is gone.
    model.setUpdateFlowOpen(false);
    QCOMPARE(model.switchTo(static_cast<int>(first.value().id)), QString{});
    QCOMPARE(model.switchError(), QString{});
    QCOMPARE(m_controller->activeProfileId(), first.value().id);
}

void TestProfilesFlow::duplicatingAnExampleMakesAnIndependentCopy()
{
    const application::Result<Profile> example =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(example.ok(), example.error().c_str());

    const std::vector<Asset> sourceAssets = m_controller->assets();
    const std::vector<Liability> sourceLiabilities = m_controller->liabilities();
    const std::vector<application::MonthFlowRecord> sourceFlows = m_controller->recentFlows(24);
    QVERIFY(!sourceLiabilities.empty());

    const std::optional<Money> sourceMortgage =
        m_controller->latestDebtBalance(sourceLiabilities.front().id);
    QVERIFY(sourceMortgage.has_value());

    const application::Result<Profile> copy =
        m_controller->duplicateProfile(example.value().id, "Our household");
    QVERIFY2(copy.ok(), copy.error().c_str());

    // The copy is the household's own from the start: duplicating an example is
    // how it is adopted, so it is not an example any more.
    QVERIFY(!copy.value().isExample);
    QCOMPARE(QString::fromStdString(copy.value().displayName), QStringLiteral("Our household"));
    QCOMPARE(m_controller->activeProfileId(), copy.value().id);
    QCOMPARE(static_cast<int>(copy.value().phase), static_cast<int>(example.value().phase));

    const std::vector<Asset> copiedAssets = m_controller->assets();
    QCOMPARE(copiedAssets.size(), sourceAssets.size());
    for (const Asset& asset : copiedAssets) {
        const bool sharesAnId =
            std::any_of(sourceAssets.cbegin(), sourceAssets.cend(),
                        [&asset](const Asset& source) { return source.id == asset.id; });
        QVERIFY2(!sharesAnId, asset.id.c_str());
    }
    QCOMPARE(m_controller->recentFlows(24).size(), sourceFlows.size());

    const std::vector<Liability> copiedLiabilities = m_controller->liabilities();
    QCOMPARE(copiedLiabilities.size(), sourceLiabilities.size());
    const std::optional<Money> copiedMortgage =
        m_controller->latestDebtBalance(copiedLiabilities.front().id);
    QVERIFY(copiedMortgage.has_value());
    QCOMPARE(minorOf(*copiedMortgage), minorOf(*sourceMortgage));

    // The source keeps its own rows and its example flag.
    const std::vector<application::ProfileSummary> listed = m_controller->profiles();
    QCOMPARE(listed.size(), std::size_t{2});
    const auto byName = [&listed](const std::string& name) {
        return std::find_if(listed.cbegin(), listed.cend(),
                            [&name](const application::ProfileSummary& summary) {
                                return summary.displayName == name;
                            });
    };
    const auto original = byName("Example: building wealth");
    const auto duplicate = byName("Our household");
    QVERIFY(original != listed.cend());
    QVERIFY(duplicate != listed.cend());
    QVERIFY(original->isExample);
    QVERIFY(!duplicate->isExample);
}

void TestProfilesFlow::deletingRemovesTheProfileAndItsRowsButNeverTheLast()
{
    const application::Result<Profile> first =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(first.ok(), first.error().c_str());
    const application::Result<Profile> second =
        m_controller->createExampleProfile(application::ExampleProfile::Preserving);
    QVERIFY2(second.ok(), second.error().c_str());

    QCOMPARE(m_controller->profiles().size(), std::size_t{2});

    // One action removes the example and everything recorded under it.
    QVERIFY(m_controller->removeProfile(first.value().id).ok());
    const std::vector<application::ProfileSummary> left = m_controller->profiles();
    QCOMPARE(left.size(), std::size_t{1});
    QCOMPARE(left.front().id, second.value().id);

    // The repositories follow the household that took over, so the screens show
    // the surviving one and not an empty leftover.
    QCOMPARE(m_controller->activeProfileId(), second.value().id);
    QCOMPARE(minorOf(bucketBalance(m_controller->dashboardSnapshot(), Bucket::Preserve)),
             kronor(7200000));

    // The last profile cannot be deleted, and the refusal says why.
    const application::Status refused = m_controller->removeProfile(second.value().id);
    QVERIFY(!refused.ok());
    QVERIFY(refused.error().find("last profile") != std::string::npos);
    QCOMPARE(m_controller->profiles().size(), std::size_t{1});
}

void TestProfilesFlow::theBackupCarriesTheExampleFlag()
{
    const application::Result<Profile> example =
        m_controller->createExampleProfile(application::ExampleProfile::BuildingWealth);
    QVERIFY2(example.ok(), example.error().c_str());
    const application::Result<Profile> own =
        m_controller->duplicateProfile(example.value().id, "Our household");
    QVERIFY2(own.ok(), own.error().c_str());

    const QString path = m_dataDirectory->filePath(QStringLiteral("backup.json"));
    const application::Result<int> written = m_controller->exportBackup(path);
    QVERIFY2(written.ok(), written.error().c_str());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();

    const application::Result<infrastructure::json::Backup> parsed =
        infrastructure::json::backupFromJson(bytes.toStdString());
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    QCOMPARE(parsed.value().profiles.size(), std::size_t{2});

    const auto flagOf = [&parsed](const std::string& name) {
        const auto found =
            std::find_if(parsed.value().profiles.cbegin(), parsed.value().profiles.cend(),
                         [&name](const infrastructure::json::BackupData& data) {
                             return data.profile.displayName == name;
                         });
        return found == parsed.value().profiles.cend()
                   ? std::optional<bool>{}
                   : std::optional<bool>{found->profile.isExample};
    };

    const std::optional<bool> exampleFlag = flagOf("Example: building wealth");
    const std::optional<bool> ownFlag = flagOf("Our household");
    QVERIFY(exampleFlag.has_value());
    QVERIFY(ownFlag.has_value());
    QCOMPARE(*exampleFlag, true);
    QCOMPARE(*ownFlag, false);
}

QTEST_GUILESS_MAIN(TestProfilesFlow)

#include "tst_profiles_flow.moc"
