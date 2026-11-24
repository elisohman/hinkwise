// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "domain/asset.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "presentation/accounts_view_model.h"
#include "presentation/app_controller.h"
#include "presentation/update_flow_view_model.h"
#include "presentation/wizard_view_model.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>
#include <QVector>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::presentation;

namespace {

/// Runs one statement on the household's file through a connection of its own,
/// and returns the error, empty when it ran.
QString runOnTheFile(const QString& path, const QString& statement)
{
    auto opened = infrastructure::sqlite::Database::open(path);
    if (!opened.ok())
        return QString::fromStdString(opened.error());

    QSqlQuery query{opened.value().connection()};
    if (!query.exec(statement))
        return query.lastError().text();
    return {};
}

/// A trigger that makes the storage refuse one particular write, dropped again
/// when the guard goes out of scope so that a failed assertion cannot leave the
/// file refusing writes for the tests after it.
///
/// Nothing a household can type makes the real repositories fail on the last
/// write of these operations - a form reads every field before it writes
/// anything - so this is how a failure halfway through is arranged without
/// replacing the database with a double.
class RefusingTrigger {
public:
    RefusingTrigger(const QString& path, const QString& name, const QString& create)
        : m_path(path)
        , m_name(name)
    {
        m_error = runOnTheFile(m_path, create);
    }

    ~RefusingTrigger() { runOnTheFile(m_path, QStringLiteral("DROP TRIGGER IF EXISTS ") + m_name); }

    RefusingTrigger(const RefusingTrigger&) = delete;
    RefusingTrigger& operator=(const RefusingTrigger&) = delete;

    /// Empty when the trigger is in place.
    const QString& error() const { return m_error; }

private:
    QString m_path;
    QString m_name;
    QString m_error;
};

int rowNamed(const QVector<BalanceEntryRow>& rows, const QString& name)
{
    for (int row = 0; row < rows.size(); ++row) {
        if (rows.at(row).name == name)
            return row;
    }
    return -1;
}

} // namespace

/// Every screen decision that writes more than one row is tested here against a
/// real database, because the guarantee being tested - all of it or none of it -
/// belongs to the transaction and not to the view model. Each test makes the
/// last write fail and then reads the database back: the rows written before the
/// failure have to be gone.
class TestMultiWriteFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void aRefusedBalanceLeavesNoHouseholdBehind();
    void theWizardStoresTheHouseholdInOneCommit();
    void aRefusedBalanceLeavesNoDebt();
    void anUnknownAccountLeavesNoBalanceForTheMonth();
    void aRefusedBalanceLeavesTheWholeMonthUnrecorded();

private:
    QTemporaryDir m_dataDirectory;
    std::unique_ptr<AppController> m_controller;
};

void TestMultiWriteFlow::initTestCase()
{
    QVERIFY(m_dataDirectory.isValid());
    // The database path comes from QStandardPaths, so this suite owns a data
    // directory instead of the household's own.
    QVERIFY(qputenv("XDG_DATA_HOME", m_dataDirectory.path().toUtf8()));

    QCoreApplication::setOrganizationName(QStringLiteral("elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise-multi-write-test"));

    m_controller = std::make_unique<AppController>(nullptr);
    QVERIFY(m_controller->initialise());
    QCOMPARE(m_controller->lastError(), QString{});
}

/// The wizard writes the profile first, then the accounts, the month's flow and
/// a balance per account. With the last balance refused, the household must not
/// exist at all: onboarding reads a stored profile as "set up", so a half-built
/// household hides the only screen that can finish it.
void TestMultiWriteFlow::aRefusedBalanceLeavesNoHouseholdBehind()
{
    RefusingTrigger refuse{
        m_controller->databasePath(), QStringLiteral("refuse_the_last_valuation"),
        QStringLiteral("CREATE TRIGGER refuse_the_last_valuation BEFORE INSERT ON valuation "
                       "WHEN NEW.asset_id = (SELECT id FROM asset WHERE name = 'Refused fund') "
                       "BEGIN SELECT RAISE(ABORT, 'the test refuses this valuation'); END")};
    QVERIFY2(refuse.error().isEmpty(), qPrintable(refuse.error()));

    WizardViewModel wizard;
    wizard.setCurrencyIndex(1); // EUR
    wizard.setPhaseIndex(2);    // Preserving
    wizard.accounts()->setName(0, QStringLiteral("Cash"));
    wizard.accounts()->setName(1, QStringLiteral("Refused fund"));
    wizard.setIncomeText(QStringLiteral("30000"));
    wizard.setNeedsText(QStringLiteral("15000"));
    wizard.setWantsText(QStringLiteral("9000"));
    wizard.accounts()->setBalanceText(0, QStringLiteral("25000"));
    wizard.accounts()->setBalanceText(1, QStringLiteral("100000"));
    for (int step = 0; step < wizard.stepCount() - 1; ++step)
        wizard.next();
    QVERIFY(wizard.canCreate());

    QVERIFY(!wizard.createEconomy());
    QVERIFY(!wizard.createError().isEmpty());

    // The profile, both accounts and the flow were written before the refusal,
    // and none of them survived it.
    QVERIFY(!m_controller->profile().has_value());
    QVERIFY(m_controller->assets().empty());
    QVERIFY(!m_controller->monthFlow(AppController::currentMonth()).has_value());
}

/// The other half of the boundary: when every write succeeds, the single commit
/// at the end is what stores them, so all four kinds of row are there.
void TestMultiWriteFlow::theWizardStoresTheHouseholdInOneCommit()
{
    WizardViewModel wizard;
    wizard.setCurrencyIndex(1); // EUR
    wizard.setPhaseIndex(2);    // Preserving
    wizard.accounts()->setName(0, QStringLiteral("Household cash"));
    wizard.accounts()->setName(1, QStringLiteral("Household fund"));
    wizard.setIncomeText(QStringLiteral("30000"));
    wizard.setNeedsText(QStringLiteral("15000"));
    wizard.setWantsText(QStringLiteral("9000"));
    wizard.accounts()->setBalanceText(0, QStringLiteral("25000"));
    wizard.accounts()->setBalanceText(1, QStringLiteral("100000"));
    for (int step = 0; step < wizard.stepCount() - 1; ++step)
        wizard.next();
    QVERIFY(wizard.canCreate());

    QVERIFY2(wizard.createEconomy(), qPrintable(wizard.createError()));

    const std::optional<domain::Profile> profile = m_controller->profile();
    QVERIFY(profile.has_value());
    QCOMPARE(profile->baseCurrency, domain::Currency::Eur);
    QCOMPARE(profile->phase, domain::Phase::Preserving);
    QCOMPARE(m_controller->assets().size(), std::size_t{2});

    const domain::YearMonth month = AppController::currentMonth();
    const std::optional<domain::MonthFlow> flow = m_controller->monthFlow(month);
    QVERIFY(flow.has_value());
    QCOMPARE(flow->income.minorUnits(), std::int64_t{3000000});

    for (const domain::Asset& asset : m_controller->assets())
        QVERIFY(m_controller->assetBalanceAt(asset.id, month).has_value());
}

/// A debt and the balance it was created with are one decision. The loan is
/// written first, so a refused balance used to leave a mortgage with no figure
/// against it - a debt the plan cannot say anything about.
void TestMultiWriteFlow::aRefusedBalanceLeavesNoDebt()
{
    AccountsViewModel accounts;
    QCOMPARE(accounts.debts()->count(), 0);

    RefusingTrigger refuse{
        m_controller->databasePath(), QStringLiteral("refuse_the_debt_balance"),
        QStringLiteral("CREATE TRIGGER refuse_the_debt_balance BEFORE INSERT ON debt_balance "
                       "BEGIN SELECT RAISE(ABORT, 'the test refuses this balance'); END")};
    QVERIFY2(refuse.error().isEmpty(), qPrintable(refuse.error()));

    const QString failure = accounts.addDebt(
        QStringLiteral("Mortgage"), static_cast<int>(domain::LiabilityKind::Mortgage),
        QStringLiteral("4,5"), accounts.fixedAmortisationStyleIndex(), QString{}, QString{},
        QString{}, QStringLiteral("2000000"));
    QVERIFY(!failure.isEmpty());

    QVERIFY(m_controller->liabilities().empty());
    QCOMPARE(accounts.debts()->count(), 0);
}

/// One month recorded from the buckets page is one write per account. The
/// failure here is an account that is not there - a row the page was showing
/// when something else removed it - and it sorts after every id the household
/// has, so it is the last write of the month.
void TestMultiWriteFlow::anUnknownAccountLeavesNoBalanceForTheMonth()
{
    const domain::Asset cash =
        domain::makeAsset("month_cash", "Month cash", domain::AssetKind::BankAccount);
    QVERIFY2(m_controller->addAsset(cash).ok(), "asset");

    AccountsViewModel accounts;
    QVariantMap balances;
    balances.insert(QString::fromStdString(cash.id), QStringLiteral("4321"));
    balances.insert(QStringLiteral("zz-no-such-account"), QStringLiteral("1000"));

    QVERIFY(!accounts.recordMonthBalances(balances).isEmpty());

    // The real account's figure was written before the failure, and the failed
    // month took it back: nothing of the month is left.
    QVERIFY(!m_controller->latestAssetBalance(cash.id).has_value());
}

/// The update flow writes one row per asset and per debt. The refused row is the
/// last one it writes, so the accounts recorded before it are already in the
/// database when the save fails - and none of them survives the rollback.
void TestMultiWriteFlow::aRefusedBalanceLeavesTheWholeMonthUnrecorded()
{
    for (const domain::Asset& asset :
         {domain::makeAsset("alpha_cash", "Alpha cash", domain::AssetKind::BankAccount),
          domain::makeAsset("zulu_fund", "Zulu fund", domain::AssetKind::Fund)}) {
        QVERIFY2(m_controller->addAsset(asset).ok(), "asset");
    }

    RefusingTrigger refuse{
        m_controller->databasePath(), QStringLiteral("refuse_the_last_balance"),
        QStringLiteral("CREATE TRIGGER refuse_the_last_balance BEFORE INSERT ON valuation "
                       "WHEN NEW.asset_id = 'zulu_fund' "
                       "BEGIN SELECT RAISE(ABORT, 'the test refuses this valuation'); END")};
    QVERIFY2(refuse.error().isEmpty(), qPrintable(refuse.error()));

    UpdateFlowViewModel flow;
    const QVector<BalanceEntryRow> rows = flow.balances()->rows();
    const int alphaRow = rowNamed(rows, QStringLiteral("Alpha cash"));
    const int zuluRow = rowNamed(rows, QStringLiteral("Zulu fund"));
    QVERIFY(alphaRow >= 0);
    QVERIFY(zuluRow >= 0);
    // Rows are written in the order they are listed, so the refused one is last.
    QVERIFY(alphaRow < zuluRow);

    flow.balances()->setAmountText(alphaRow, QStringLiteral("12345"));
    flow.balances()->setAmountText(zuluRow, QStringLiteral("67890"));

    QVERIFY(!flow.saveBalances().isEmpty());

    QVERIFY(!m_controller->latestAssetBalance("alpha_cash").has_value());
    QVERIFY(!m_controller->latestAssetBalance("zulu_fund").has_value());
}

QTEST_GUILESS_MAIN(TestMultiWriteFlow)

#include "tst_multi_write_flow.moc"
