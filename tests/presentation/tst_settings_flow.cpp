// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/dashboard.h"
#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/household.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/phase.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/targets.h"
#include "domain/year_month.h"
#include "infrastructure/json/backup.h"
#include "presentation/app_controller.h"
#include "presentation/input_text.h"
#include "presentation/settings_view_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::domain;
using namespace hinkwise::presentation;

namespace {

Money sek(std::int64_t minorUnits)
{
    return Money{minorUnits, Currency::Sek};
}

const PhaseComparisonRow* rowLabelled(const QVector<PhaseComparisonRow>& rows, const QString& label)
{
    const auto found =
        std::find_if(rows.cbegin(), rows.cend(),
                     [&label](const PhaseComparisonRow& row) { return row.label == label; });
    return found == rows.cend() ? nullptr : &(*found);
}

} // namespace

/// The settings page against the real database: the phase switcher writes only
/// when it is confirmed, the jurisdiction round-trips through the profile, and
/// a backup leaves with every recorded month and valuation in it.
class TestSettingsFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void comparisonShowsWhatEachPhaseDoesToThisHousehold();
    void twoPhasesWithOneSizingTableChangeNothing();
    void choosingAPhaseWritesNothingUntilItIsConfirmed();
    void jurisdictionRoundTripsAndRecalculates();
    void jurisdictionRefusesWhatIsNotAnAssumption();
    void displayNameIsRequired();
    void exportWritesEveryRecordedPoint();
    void exportRefusesTheDatabaseFile();
    void exportRefusesADatabaseSidecar();
    void exportNeedsAFile();

private:
    QTemporaryDir m_dataDirectory;
    std::unique_ptr<AppController> m_controller;
};

void TestSettingsFlow::initTestCase()
{
    QVERIFY(m_dataDirectory.isValid());
    QVERIFY(qputenv("XDG_DATA_HOME", m_dataDirectory.path().toUtf8()));

    QCoreApplication::setOrganizationName(QStringLiteral("elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise-test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // The amounts are formatted with the default locale, so pin it: the test has
    // to read the same text on every machine.
    QLocale::setDefault(QLocale{QStringLiteral("sv_SE")});

    m_controller = std::make_unique<AppController>(nullptr);
    QVERIFY(m_controller->initialise());
    QCOMPARE(m_controller->lastError(), QString{});

    QVERIFY(m_controller->createHousehold(Currency::Sek, Phase::GettingStarted).ok());
    QVERIFY(
        m_controller->addAsset(makeAsset("konto", "Savings account", AssetKind::BankAccount)).ok());
    QVERIFY(m_controller->addAsset(makeAsset("fund", "Global fund", AssetKind::Fund)).ok());
    QVERIFY(m_controller
                ->addLiability(makeLiability("bolan", "Mortgage", LiabilityKind::Mortgage,
                                             Percentage::fromBasisPoints(420)))
                .ok());
    QVERIFY(
        m_controller
            ->addPlannedExpense(PlannedExpense{"tak", "Roof", sek(15000000), YearMonth{2028, 5}})
            .ok());

    const YearMonth thisMonth = AppController::currentMonth();
    QVERIFY(m_controller
                ->recordMonth(thisMonth,
                              MonthFlow{sek(3000000), sek(1500000), sek(500000), sek(1000000)})
                .ok());

    // Three months of one asset and one of another, so the backup has a history
    // to lose rather than a single point.
    QVERIFY(m_controller->recordValuation("fund", YearMonth{2026, 1}, sek(10000000)).ok());
    QVERIFY(m_controller->recordValuation("fund", YearMonth{2026, 2}, sek(11000000)).ok());
    QVERIFY(m_controller->recordValuation("fund", YearMonth{2026, 3}, sek(12000000)).ok());
    QVERIFY(m_controller->recordValuation("konto", YearMonth{2026, 3}, sek(5000000)).ok());
    QVERIFY(m_controller->recordDebtBalance("bolan", YearMonth{2026, 1}, sek(250000000)).ok());
    QVERIFY(m_controller->recordDebtBalance("bolan", YearMonth{2026, 2}, sek(248000000)).ok());
}

void TestSettingsFlow::comparisonShowsWhatEachPhaseDoesToThisHousehold()
{
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    const QVector<PhaseComparisonRow> rows = phaseComparisonOf(snapshot, Phase::Preserving);

    QCOMPARE(rows.size(), 6);
    QCOMPARE(rows.at(0).label, QStringLiteral("Buffer target"));
    QCOMPARE(rows.at(1).label, QStringLiteral("Buffer gap today"));
    QCOMPARE(rows.at(2).label, QStringLiteral("Preserve target"));
    QCOMPARE(rows.at(3).label, QStringLiteral("Growth"));
    QCOMPARE(rows.at(4).label, QStringLiteral("Play"));
    QCOMPARE(rows.at(5).label, QStringLiteral("Where money moves"));

    // The household spends 20 000 a month, so the accumulating buffer band is
    // 10 000 – 120 000 and the preserving one is 1 – 5 years of 240 000.
    const PhaseComparisonRow* buffer = rowLabelled(rows, QStringLiteral("Buffer target"));
    QVERIFY(buffer != nullptr);
    QVERIFY(buffer->changed);
    QVERIFY(buffer->before.startsWith(QStringLiteral("0,5–6 months of expenses · ")));
    QVERIFY(buffer->before.contains(formatMoney(sek(1000000))));
    QVERIFY(buffer->before.contains(formatMoney(sek(12000000))));
    QVERIFY(buffer->after.contains(QStringLiteral("1–5 years of expenses")));
    QVERIFY(buffer->after.contains(formatMoney(sek(24000000))));
    QVERIFY(buffer->after.contains(formatMoney(sek(120000000))));

    // The buffer holds 50 000, so the gap is the point in each band minus that.
    const PhaseComparisonRow* gap = rowLabelled(rows, QStringLiteral("Buffer gap today"));
    QVERIFY(gap != nullptr);
    QVERIFY(gap->changed);
    QVERIFY(gap->before.contains(formatMoney(sek(1500000))));
    QVERIFY(gap->after.contains(formatMoney(sek(67000000))));

    // Preserve stops being covered by housing and pension and becomes 25 – 35
    // years of expenses.
    const PhaseComparisonRow* preserve = rowLabelled(rows, QStringLiteral("Preserve target"));
    QVERIFY(preserve != nullptr);
    QVERIFY(preserve->changed);
    QCOMPARE(preserve->before, QStringLiteral("Not a target"));
    QVERIFY(preserve->after.contains(QStringLiteral("25–35 years of expenses")));
    QVERIFY(preserve->after.contains(formatMoney(sek(600000000))));
    QVERIFY(preserve->after.contains(formatMoney(sek(840000000))));

    // Growth stops being a target.
    const PhaseComparisonRow* growth = rowLabelled(rows, QStringLiteral("Growth"));
    QVERIFY(growth != nullptr);
    QVERIFY(growth->changed);
    QCOMPARE(growth->before, QStringLiteral("As large as possible"));
    QCOMPARE(growth->after, QStringLiteral("Optional"));

    // Play is capped in both phases, and the cap is not a target: the row says
    // so and does not move.
    const PhaseComparisonRow* play = rowLabelled(rows, QStringLiteral("Play"));
    QVERIFY(play != nullptr);
    QVERIFY(!play->changed);
    QCOMPARE(play->before, play->after);
    QVERIFY(play->before.contains(QStringLiteral("10 %")));
    QVERIFY(play->before.contains(QStringLiteral("not a target")));

    const PhaseComparisonRow* flow = rowLabelled(rows, QStringLiteral("Where money moves"));
    QVERIFY(flow != nullptr);
    QVERIFY(flow->changed);
    QCOMPARE(flow->before, QStringLiteral("Money fills the buckets"));
    QVERIFY(flow->after.contains(QStringLiteral("drawn down")));
}

void TestSettingsFlow::twoPhasesWithOneSizingTableChangeNothing()
{
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    const QVector<PhaseComparisonRow> rows = phaseComparisonOf(snapshot, Phase::BuildingWealth);

    // Getting started and building wealth share one sizing table and one flow
    // direction, so the sheet has nothing to announce - which it says rather
    // than inventing a difference.
    QCOMPARE(rows.size(), 6);
    QVERIFY(std::none_of(rows.cbegin(), rows.cend(),
                         [](const PhaseComparisonRow& row) { return row.changed; }));
}

void TestSettingsFlow::choosingAPhaseWritesNothingUntilItIsConfirmed()
{
    SettingsViewModel settings;
    QVERIFY(settings.hasProfile());
    QCOMPARE(settings.phaseIndex(), 0);
    QCOMPARE(settings.phaseLabel(), QStringLiteral("Getting started"));
    QCOMPARE(static_cast<int>(settings.phaseChoices().size()), 4);

    // Choosing the phase the household is already in opens nothing.
    settings.beginPhaseChange(0);
    QVERIFY(!settings.comparisonVisible());

    settings.beginPhaseChange(2);
    QVERIFY(settings.comparisonVisible());
    QCOMPARE(settings.pendingPhaseIndex(), 2);
    QCOMPARE(settings.pendingPhaseLabel(), QStringLiteral("Preserving"));
    QVERIFY(!settings.comparisonRows().isEmpty());
    QVERIFY(settings.comparisonChanges());

    // Nothing has been written, and cancelling leaves it that way.
    QCOMPARE(m_controller->profile()->phase, Phase::GettingStarted);
    QVERIFY(m_controller->phaseHistory().empty());
    settings.cancelPhaseChange();
    QVERIFY(!settings.comparisonVisible());
    QCOMPARE(m_controller->profile()->phase, Phase::GettingStarted);

    // Confirming goes through the use case: the profile moves, the history
    // gains an entry, and the page that draws the phase follows.
    settings.beginPhaseChange(1);
    QCOMPARE(settings.confirmPhaseChange(), QString{});
    QVERIFY(!settings.comparisonVisible());
    QCOMPARE(m_controller->profile()->phase, Phase::BuildingWealth);
    QCOMPARE(m_controller->phaseHistory().size(), std::size_t{1});
    QCOMPARE(m_controller->phaseHistory().front().phase, Phase::BuildingWealth);
    QCOMPARE(m_controller->dashboardSnapshot().profile.phase, Phase::BuildingWealth);
    QCOMPARE(settings.phaseIndex(), 1);
    QCOMPARE(settings.phaseLabel(), QStringLiteral("Building wealth"));

    // The history the page lists is newest first and names the phase.
    QCOMPARE(static_cast<int>(settings.phaseHistory().size()), 1);
    const QVariantMap entry = settings.phaseHistory().front().toMap();
    QCOMPARE(entry.value(QStringLiteral("label")).toString(), QStringLiteral("Building wealth"));
    QVERIFY(!entry.value(QStringLiteral("when")).toString().isEmpty());
}

void TestSettingsFlow::jurisdictionRoundTripsAndRecalculates()
{
    SettingsViewModel settings;
    QVERIFY(settings.hasProfile());
    QCOMPARE(settings.safeWithdrawalRateText(), QStringLiteral("4,00"));
    QCOMPARE(settings.capitalTaxDragText(), QStringLiteral("1,00"));
    QCOMPARE(settings.fiMultiplierLowText(), QStringLiteral("21"));
    QCOMPARE(settings.fiMultiplierDefaultText(), QStringLiteral("25"));
    QCOMPARE(settings.fiMultiplierHighText(), QStringLiteral("33"));
    QVERIFY(!settings.jurisdictionDirty());

    settings.setSafeWithdrawalRateText(QStringLiteral("3,5"));
    QVERIFY(settings.jurisdictionDirty());
    QCOMPARE(settings.saveJurisdiction(), QString{});

    const std::optional<Profile> saved = m_controller->profile();
    QVERIFY(saved.has_value());
    QCOMPARE(saved->jurisdiction.safeWithdrawalRate.basisPoints(), std::int32_t{350});
    QCOMPARE(saved->jurisdiction.fiMultiplierDefault, 25);
    // The field shows the stored value in the canonical spelling again, and the
    // page is no longer dirty.
    QCOMPARE(settings.safeWithdrawalRateText(), QStringLiteral("3,50"));
    QVERIFY(!settings.jurisdictionDirty());

    // Every multiplier is editable, and the dashboard reads them back.
    settings.setFiMultiplierDefaultText(QStringLiteral("24"));
    settings.setFiMultiplierHighText(QStringLiteral("34"));
    QCOMPARE(settings.saveJurisdiction(), QString{});
    QCOMPARE(m_controller->profile()->jurisdiction.fiMultiplierDefault, 24);
    QCOMPARE(settings.fiMultiplierHighText(), QStringLiteral("34"));

    // Resetting fills the fields but writes nothing; saving applies them.
    settings.resetJurisdictionToSwedishDefaults();
    QCOMPARE(settings.safeWithdrawalRateText(), QStringLiteral("4,00"));
    QVERIFY(settings.jurisdictionDirty());
    QCOMPARE(m_controller->profile()->jurisdiction.safeWithdrawalRate.basisPoints(),
             std::int32_t{350});
    QCOMPARE(settings.saveJurisdiction(), QString{});
    QCOMPARE(m_controller->profile()->jurisdiction.safeWithdrawalRate.basisPoints(),
             std::int32_t{400});
    QVERIFY(!settings.jurisdictionDirty());
}

void TestSettingsFlow::jurisdictionRefusesWhatIsNotAnAssumption()
{
    SettingsViewModel settings;

    // A withdrawal rate of zero would divide by nothing.
    settings.setSafeWithdrawalRateText(QStringLiteral("0"));
    QVERIFY(!settings.saveJurisdiction().isEmpty());
    QVERIFY(!settings.safeWithdrawalRateError().isEmpty());
    QCOMPARE(m_controller->profile()->jurisdiction.safeWithdrawalRate.basisPoints(),
             std::int32_t{400});

    // Text that is not a rate at all is reported, not guessed at.
    settings.setSafeWithdrawalRateText(QStringLiteral("4 %"));
    QVERIFY(!settings.saveJurisdiction().isEmpty());
    QVERIFY(!settings.safeWithdrawalRateError().isEmpty());

    settings.setSafeWithdrawalRateText(QStringLiteral("4"));
    settings.setFiMultiplierLowText(QStringLiteral("30"));
    QVERIFY(!settings.saveJurisdiction().isEmpty());
    QVERIFY(!settings.multiplierError().isEmpty());
    QCOMPARE(m_controller->profile()->jurisdiction.fiMultiplierDefault, 25);
}

void TestSettingsFlow::displayNameIsRequired()
{
    SettingsViewModel settings;
    QVERIFY(!settings.displayNameDirty());

    settings.setDisplayNameText(QStringLiteral("   "));
    QVERIFY(!settings.saveDisplayName().isEmpty());
    QVERIFY(!settings.displayNameError().isEmpty());
    QCOMPARE(QString::fromStdString(m_controller->profile()->displayName),
             QStringLiteral("Household"));

    settings.setDisplayNameText(QStringLiteral("  Familjen  "));
    QVERIFY(settings.displayNameDirty());
    QCOMPARE(settings.saveDisplayName(), QString{});
    QCOMPARE(QString::fromStdString(m_controller->profile()->displayName),
             QStringLiteral("Familjen"));
    // The stored name is what the field shows, trimmed.
    QCOMPARE(settings.displayNameText(), QStringLiteral("Familjen"));
    QVERIFY(!settings.displayNameDirty());
}

void TestSettingsFlow::exportWritesEveryRecordedPoint()
{
    SettingsViewModel settings;
    const QString path = m_dataDirectory.path() + QStringLiteral("/backup.json");

    QCOMPARE(settings.exportBackup(QUrl::fromLocalFile(path)), QString{});
    QVERIFY(settings.backupError().isEmpty());
    QVERIFY(settings.backupMessage().contains(path));

    // A backup holds the whole balance sheet, so it is readable by its owner
    // alone.
    const QFile::Permissions permissions = QFileInfo{path}.permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const application::Result<infrastructure::json::BackupData> parsed =
        infrastructure::json::backupFromJson(file.readAll().toStdString());
    QVERIFY2(parsed.ok(), parsed.error().c_str());
    const infrastructure::json::BackupData& data = parsed.value();

    QCOMPARE(QString::fromStdString(data.profile.displayName), QStringLiteral("Familjen"));
    QCOMPARE(data.assets.size(), std::size_t{2});
    QCOMPARE(data.liabilities.size(), std::size_t{1});
    QCOMPARE(data.plannedExpenses.size(), std::size_t{1});
    QCOMPARE(data.monthFlows.size(), std::size_t{1});
    QCOMPARE(data.phaseHistory.size(), std::size_t{1});
    QCOMPARE(data.assetValuations.size(), std::size_t{2});
    QCOMPARE(data.liabilityBalances.size(), std::size_t{1});

    // The whole history, not just the latest balance: this is the part a
    // household cannot recreate.
    const auto fund = std::find_if(
        data.assetValuations.cbegin(), data.assetValuations.cend(),
        [](const infrastructure::json::AssetValuations& entry) { return entry.assetId == "fund"; });
    QVERIFY(fund != data.assetValuations.cend());
    QCOMPARE(fund->points.size(), std::size_t{3});
    QCOMPARE(fund->points.at(0).amount.minorUnits(), std::int64_t{10000000});
    QCOMPARE(fund->points.at(1).amount.minorUnits(), std::int64_t{11000000});
    QCOMPARE(fund->points.at(2).amount.minorUnits(), std::int64_t{12000000});
    QCOMPARE(QString::fromStdString(fund->points.at(2).month.toString()),
             QStringLiteral("2026-03"));

    const infrastructure::json::LiabilityBalances& mortgage = data.liabilityBalances.front();
    QCOMPARE(QString::fromStdString(mortgage.liabilityId), QStringLiteral("bolan"));
    QCOMPARE(mortgage.points.size(), std::size_t{2});
    QCOMPARE(mortgage.points.at(1).amount.minorUnits(), std::int64_t{248000000});

    // The weekday the file was written is not part of the format, so a second
    // export of the same household produces the same text.
    const QString first = QString::fromStdString(infrastructure::json::toJson(data));
    QCOMPARE(settings.exportBackup(QUrl::fromLocalFile(path)), QString{});
    QFile again{path};
    QVERIFY(again.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromStdString(again.readAll().toStdString()), first);
}

void TestSettingsFlow::exportRefusesTheDatabaseFile()
{
    SettingsViewModel settings;
    const QString databasePath = m_controller->databasePath();
    QVERIFY(!databasePath.isEmpty());

    QFile databaseFile{databasePath};
    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    const QByteArray before = databaseFile.readAll();
    databaseFile.close();
    QVERIFY(!before.isEmpty());

    // The database path is a valid-looking file to save to. Choosing it must
    // not replace SQLite with JSON.
    QVERIFY(!settings.exportBackup(QUrl::fromLocalFile(databasePath)).isEmpty());
    QVERIFY(settings.backupError().contains(databasePath));
    QVERIFY(settings.backupMessage().isEmpty());

    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    QCOMPARE(databaseFile.readAll(), before);
    databaseFile.close();

    // The file is still a database: the household reads back and the dashboard
    // still describes it.
    QVERIFY(m_controller->profile().has_value());
    QVERIFY(m_controller->dashboardSnapshot().hasProfile);
}

void TestSettingsFlow::exportRefusesADatabaseSidecar()
{
    SettingsViewModel settings;
    const QString databasePath = m_controller->databasePath();
    const QString writeAheadLog = databasePath + QStringLiteral("-wal");

    // The sidecar may not exist yet - SQLite creates it on demand - but writing
    // a backup with that name would still hand SQLite a JSON file.
    QVERIFY(!settings.exportBackup(QUrl::fromLocalFile(writeAheadLog)).isEmpty());
    QVERIFY(settings.backupError().contains(databasePath));
    QVERIFY(!QFile::exists(writeAheadLog));
}

void TestSettingsFlow::exportNeedsAFile()
{
    SettingsViewModel settings;
    QVERIFY(!settings.exportBackup(QUrl{}).isEmpty());
    QVERIFY(!settings.backupError().isEmpty());
    QVERIFY(settings.backupMessage().isEmpty());
}

QTEST_GUILESS_MAIN(TestSettingsFlow)

#include "tst_settings_flow.moc"
