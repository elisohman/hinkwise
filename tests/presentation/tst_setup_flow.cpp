// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presentation/accounts_view_model.h"
#include "presentation/app_controller.h"
#include "presentation/cashflow_view_model.h"
#include "presentation/dashboard_view_model.h"
#include "presentation/input_text.h"
#include "presentation/plan_view_model.h"
#include "presentation/review_view_model.h"
#include "presentation/update_flow_view_model.h"
#include "presentation/wizard_view_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::presentation;

/// The setup wizard is the only way a household comes into existence, so it is
/// tested against the real database, the real repositories and the real use
/// cases rather than a double. The pages that read and write the household -
/// buckets, cashflow, plan and the year-end review - are exercised through the
/// same controller, because a mutation that bypasses it would leave the
/// dashboard describing a household that no longer exists.
class TestSetupFlow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void startsWithoutAHousehold();
    void refusesToLeaveTheIncomeStepUnfilled();
    void accountEditsReachTheWizardsChangedSignal();
    void createsTheHouseholdItReviewed();
    void bucketsPageAddsAndValuesAnAccount();
    void bucketsPageRecordsTheWholeMonth();
    void bucketsPageEditsAndDeletes();
    void bucketsPageAddsAndRemovesADebt();
    void bucketsPageEditsADebtsTermsAndKeepsItsHistory();
    void planShowsTheCostOfTheDebt();
    void rejectsWhatTheUseCasesReject();
    void cashflowPageRecordsAndChecksTheMonth();
    void cashflowDraftSurvivesReloadAndDiscard();
    void cashflowSavingSplitValidatesAndSaves();
    void cashflowDerivesAmortisationFromTheLoanTerms();
    void dashboardLeadsWithTheBufferPoint();
    void planPageProjectsTheHousehold();
    void planDoesNotCountTheHouseAsSpendable();
    void reviewPageAddsAGoalAndProposesNothingAutomatic();
    void updateFlowRecordsBothStepsForOneBackfilledMonth();

private:
    QTemporaryDir m_dataDirectory;
    std::unique_ptr<AppController> m_controller;
};

void TestSetupFlow::initTestCase()
{
    QVERIFY(m_dataDirectory.isValid());
    // The database path comes from QStandardPaths, so the test owns a data
    // directory instead of the household's own.
    QVERIFY(qputenv("XDG_DATA_HOME", m_dataDirectory.path().toUtf8()));

    QCoreApplication::setOrganizationName(QStringLiteral("elisohman"));
    QCoreApplication::setApplicationName(QStringLiteral("hinkwise-test"));

    // The amounts are formatted with the default locale, so pin it: the test
    // has to read the same text on every machine.
    QLocale::setDefault(QLocale{QStringLiteral("sv_SE")});

    m_controller = std::make_unique<AppController>(nullptr);
    QVERIFY(m_controller->initialise());
    QCOMPARE(m_controller->lastError(), QString{});
}

void TestSetupFlow::startsWithoutAHousehold()
{
    QVERIFY(!m_controller->profile().has_value());
    QVERIFY(!m_controller->dashboardSnapshot().hasProfile);
    QVERIFY(!m_controller->dashboardSnapshot().hasFlow);
    QVERIFY(m_controller->assets().empty());
}

void TestSetupFlow::refusesToLeaveTheIncomeStepUnfilled()
{
    WizardViewModel wizard;

    QCOMPARE(wizard.step(), 0);
    QVERIFY(wizard.canGoNext());

    wizard.next();
    QCOMPARE(wizard.step(), 1);
    QVERIFY(wizard.canGoNext());

    wizard.next();
    QCOMPARE(wizard.step(), 2);
    QVERIFY(!wizard.canGoNext());
    QVERIFY(!wizard.blockingReason().isEmpty());

    // Spaces and a comma mean the same amount here as anywhere else.
    wizard.setIncomeText(QStringLiteral("None"));
    QVERIFY(!wizard.canGoNext());
    QVERIFY(!wizard.incomeError().isEmpty());

    wizard.setIncomeText(QStringLiteral("30000,00"));
    QVERIFY(!wizard.canGoNext());

    wizard.setNeedsText(QStringLiteral("15000"));
    wizard.setWantsText(QStringLiteral("9000"));
    QVERIFY(wizard.canGoNext());

    // Spending more than comes in is refused by recordMonth() too, so the step
    // stops before it can become a use case error.
    wizard.setWantsText(QStringLiteral("20000"));
    QVERIFY(!wizard.canGoNext());
    QCOMPARE(wizard.savingLabel(), QString{});
    wizard.setWantsText(QStringLiteral("9000"));
    QVERIFY(wizard.canGoNext());
    QVERIFY(!wizard.savingLabel().isEmpty());

    wizard.back();
    QCOMPARE(wizard.step(), 1);
}

void TestSetupFlow::accountEditsReachTheWizardsChangedSignal()
{
    WizardViewModel wizard;
    wizard.setIncomeText(QStringLiteral("30000"));
    wizard.setNeedsText(QStringLiteral("15000"));
    wizard.setWantsText(QStringLiteral("9000"));
    wizard.next();
    wizard.next();
    wizard.next();
    QCOMPARE(wizard.step(), 3);
    QVERIFY(!wizard.canGoNext());

    // canGoNext is notified by changed. Every edit of an account row has to
    // announce itself, or the Next button keeps the state it had before.
    QSignalSpy changed{&wizard, &WizardViewModel::changed};

    wizard.accounts()->setBalanceText(0, QStringLiteral("25000"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!wizard.canGoNext());

    wizard.accounts()->setBalanceText(1, QStringLiteral("100000"));
    QCOMPARE(changed.count(), 2);
    QVERIFY(wizard.canGoNext());

    wizard.accounts()->addRow();
    QCOMPARE(changed.count(), 3);
    QVERIFY(!wizard.canGoNext());

    wizard.accounts()->removeRow(2);
    QCOMPARE(changed.count(), 4);
    QVERIFY(wizard.canGoNext());
}

void TestSetupFlow::createsTheHouseholdItReviewed()
{
    WizardViewModel wizard;
    wizard.setCurrencyIndex(1); // EUR
    wizard.setPhaseIndex(2);    // Preserving
    wizard.setIncomeText(QStringLiteral("30000,00"));
    wizard.setNeedsText(QStringLiteral("15000"));
    wizard.setWantsText(QStringLiteral("9000"));

    // The phase step asks how the household keeps its record too, and starts
    // on the budget rather than on a monthly entry it has not agreed to.
    QCOMPARE(wizard.trackingModeIndex(), static_cast<int>(domain::TrackingMode::Planning));
    QVERIFY(!wizard.trackingModeLabel().isEmpty());

    wizard.next(); // currency
    wizard.next(); // phase
    wizard.next(); // income
    QCOMPARE(wizard.step(), 3);

    // Two rows arrive ready to be filled in: a bank account and a fund.
    QCOMPARE(wizard.accounts()->count(), 2);
    QVERIFY(!wizard.canGoNext());

    wizard.accounts()->setBalanceText(0, QStringLiteral("25000"));
    QVERIFY(!wizard.canGoNext());
    wizard.accounts()->setBalanceText(1, QStringLiteral("100000"));
    QVERIFY(wizard.canGoNext());

    // A name is required, and the reason says which row.
    const QString firstName =
        wizard.accounts()
            ->data(wizard.accounts()->index(0, 0), AccountDraftModel::NameRole)
            .toString();
    wizard.accounts()->setName(0, QString{});
    QVERIFY(!wizard.canGoNext());
    wizard.accounts()->setName(0, firstName);
    QVERIFY(wizard.canGoNext());

    wizard.next();
    QCOMPARE(wizard.step(), 4);
    QVERIFY(wizard.isReview());
    QVERIFY(wizard.canCreate());
    QVERIFY(!wizard.reviewLines().isEmpty());

    QVERIFY(wizard.createEconomy());
    QVERIFY(wizard.createError().isEmpty());

    const std::optional<domain::Profile> profile = m_controller->profile();
    QVERIFY(profile.has_value());
    QCOMPARE(profile->baseCurrency, domain::Currency::Eur);
    QCOMPARE(profile->phase, domain::Phase::Preserving);
    QCOMPARE(static_cast<int>(profile->trackingMode),
             static_cast<int>(domain::TrackingMode::Planning));

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QVERIFY(snapshot.hasProfile);
    QVERIFY(snapshot.hasFlow);
    QCOMPARE(m_controller->assets().size(), std::size_t{2});

    // The bucket comes from the kind through makeAsset(): cash in the buffer,
    // the fund in growth, and the amounts the wizard collected.
    const auto buffer = static_cast<std::size_t>(domain::Bucket::Buffer);
    const auto growth = static_cast<std::size_t>(domain::Bucket::Growth);
    QCOMPARE(snapshot.facts.sheet.perBucket[buffer].minorUnits(), std::int64_t{2500000});
    QCOMPARE(snapshot.facts.sheet.perBucket[growth].minorUnits(), std::int64_t{10000000});
    QCOMPARE(snapshot.facts.flow.income.minorUnits(), std::int64_t{3000000});
    QCOMPARE(snapshot.facts.flow.saving.minorUnits(), std::int64_t{600000});

    // Pressing Create again replaces the same rows instead of duplicating them.
    QVERIFY(wizard.createEconomy());
    QCOMPARE(m_controller->assets().size(), std::size_t{2});
}

void TestSetupFlow::bucketsPageAddsAndValuesAnAccount()
{
    AccountsViewModel accounts;
    QCOMPARE(accounts.selectedBucket(), 0);
    QCOMPARE(accounts.accounts()->count(), 1); // the bank account, in the buffer
    QCOMPARE(accounts.debts()->count(), 0);
    QCOMPARE(accounts.currencyCode(), QStringLiteral("EUR"));

    const int play = static_cast<int>(domain::Bucket::Play);
    accounts.setSelectedBucket(play);
    QCOMPARE(accounts.accounts()->count(), 0);

    // A new account starts from a kind that belongs in the bucket in view, and
    // the bucket follows the kind until the household says otherwise.
    QCOMPARE(accounts.firstKindForBucket(play), static_cast<int>(domain::AssetKind::Equity));
    QCOMPARE(accounts.defaultBucketForKind(static_cast<int>(domain::AssetKind::Fund)),
             static_cast<int>(domain::Bucket::Growth));

    QCOMPARE(accounts.addAccount(QStringLiteral("Crypto wallet"),
                                 static_cast<int>(domain::AssetKind::Crypto), play),
             QString{});
    QCOMPARE(accounts.accounts()->count(), 1);

    const QString id = accounts.accounts()
                           ->data(accounts.accounts()->index(0, 0), AccountRowModel::IdRole)
                           .toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(accounts.accounts()
                 ->data(accounts.accounts()->index(0, 0), AccountRowModel::BucketLabelRole)
                 .toString(),
             QStringLiteral("Play"));

    // Newly added, it is worth nothing until it is valued, and the row says so
    // rather than showing a zero it did not measure.
    QCOMPARE(accounts.accounts()
                 ->data(accounts.accounts()->index(0, 0), AccountRowModel::BalanceLabelRole)
                 .toString(),
             QStringLiteral("Not valued"));

    QCOMPARE(accounts.latestBalanceText(id), QString{});
    QCOMPARE(accounts.recordValuation(id, QStringLiteral("1200,50")), QString{});
    QCOMPARE(accounts.accounts()
                 ->data(accounts.accounts()->index(0, 0), AccountRowModel::HasMonthValueRole)
                 .toBool(),
             true);

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(domain::Bucket::Play)].minorUnits(),
        std::int64_t{120050});
}

void TestSetupFlow::bucketsPageRecordsTheWholeMonth()
{
    AccountsViewModel accounts;
    accounts.setSelectedBucket(0); // buffer
    QCOMPARE(accounts.accounts()->count(), 1);

    const QString id = accounts.accounts()
                           ->data(accounts.accounts()->index(0, 0), AccountRowModel::IdRole)
                           .toString();

    QVariantMap balances;
    balances.insert(id, QStringLiteral("31000"));
    QCOMPARE(accounts.recordMonthBalances(balances), QString{});

    const QString expected = formatMoneyPlain(domain::Money{3100000, domain::Currency::Eur});
    QCOMPARE(accounts.latestBalanceText(id), expected);

    // One unreadable field stops the whole form: the month is either recorded
    // or it is not.
    QVariantMap broken;
    broken.insert(id, QStringLiteral("31 000 kr"));
    QVERIFY(!accounts.recordMonthBalances(broken).isEmpty());
    QCOMPARE(accounts.latestBalanceText(id), expected);

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(snapshot.facts.sheet.perBucket[static_cast<std::size_t>(domain::Bucket::Buffer)]
                 .minorUnits(),
             std::int64_t{3100000});
}

void TestSetupFlow::bucketsPageEditsAndDeletes()
{
    AccountsViewModel accounts;
    accounts.setSelectedBucket(static_cast<int>(domain::Bucket::Play));

    const int row = 0;
    const QString id = accounts.accounts()
                           ->data(accounts.accounts()->index(row, 0), AccountRowModel::IdRole)
                           .toString();
    // Moving the account to growth keeps the valuation it already has.
    QCOMPARE(accounts.updateAccount(id, QStringLiteral("Speculative fund"),
                                    static_cast<int>(domain::AssetKind::Fund),
                                    static_cast<int>(domain::Bucket::Growth)),
             QString{});
    QCOMPARE(accounts.accounts()->count(), 0); // it is not in play any more

    accounts.setSelectedBucket(static_cast<int>(domain::Bucket::Growth));
    QCOMPARE(accounts.accounts()->count(), 2);

    QVERIFY(!accounts.updateAccount(QString{}, QStringLiteral("Unnamed"), 0, 0).isEmpty());

    QCOMPARE(accounts.removeAccount(id), QString{});
    QCOMPARE(accounts.accounts()->count(), 1);
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(domain::Bucket::Play)].minorUnits(),
        std::int64_t{0});
}

void TestSetupFlow::bucketsPageAddsAndRemovesADebt()
{
    AccountsViewModel accounts;
    QCOMPARE(accounts.debts()->count(), 0);

    QCOMPARE(accounts.addDebt(QStringLiteral("Mortgage"),
                              static_cast<int>(domain::LiabilityKind::Mortgage),
                              QStringLiteral("4,5"), accounts.fixedAmortisationStyleIndex(),
                              QString{}, QString{}, QString{}, QStringLiteral("2000000")),
             QString{});
    QCOMPARE(accounts.debts()->count(), 1);

    const QString id =
        accounts.debts()->data(accounts.debts()->index(0, 0), DebtRowModel::IdRole).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(accounts.debts()
                 ->data(accounts.debts()->index(0, 0), DebtRowModel::RateLabelRole)
                 .toString(),
             formatInterestRate(domain::InterestRate::fromPartsPer100k(4500)));

    const application::DashboardSnapshot withDebt = m_controller->dashboardSnapshot();
    QVERIFY(withDebt.facts.sheet.netWorth.isNegative());

    QCOMPARE(accounts.removeDebt(id), QString{});
    QCOMPARE(accounts.debts()->count(), 0);
    QVERIFY(!m_controller->dashboardSnapshot().facts.sheet.netWorth.isNegative());
}

void TestSetupFlow::bucketsPageEditsADebtsTermsAndKeepsItsHistory()
{
    AccountsViewModel accounts;
    const int annuity = accounts.annuityStyleIndex();

    QCOMPARE(accounts.addDebt(QStringLiteral("Student loan"),
                              static_cast<int>(domain::LiabilityKind::StudentLoan),
                              QStringLiteral("2,5"), annuity, QStringLiteral("1200"),
                              QStringLiteral("25"), QStringLiteral("18"), QStringLiteral("150000")),
             QString{});
    QCOMPARE(accounts.debts()->count(), 1);

    const QModelIndex row = accounts.debts()->index(0, 0);
    const QString id = accounts.debts()->data(row, DebtRowModel::IdRole).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(accounts.debts()->data(row, DebtRowModel::StyleIndexRole).toInt(), annuity);
    // The rate reopens without its unit, so the field reads it back unchanged.
    QCOMPARE(accounts.debts()->data(row, DebtRowModel::RateTextRole).toString(),
             QStringLiteral("2,500"));
    QCOMPARE(accounts.debts()->data(row, DebtRowModel::PaymentTextRole).toString(),
             formatMoneyPlain(domain::Money{120000, domain::Currency::Eur}));
    // The field collected 25 kr a year; the model spreads it to 2 kr a month,
    // and the field shows that year again (24 kr).
    QCOMPARE(accounts.debts()->data(row, DebtRowModel::FeeTextRole).toString(),
             formatMoneyPlain(domain::Money{2400, domain::Currency::Eur}));
    // The term is entered in years; the model stores months.
    QCOMPARE(accounts.debts()->data(row, DebtRowModel::TermYearsTextRole).toString(),
             QStringLiteral("18"));

    // An older balance is the history an edit must not throw away.
    const domain::YearMonth earlier = AppController::currentMonth().plusMonths(-2);
    QVERIFY(m_controller
                ->recordDebtBalance(id.toStdString(), earlier,
                                    domain::Money{16000000, domain::Currency::Eur})
                .ok());
    const auto historyBefore = m_controller->debtHistory(id.toStdString());
    QCOMPARE(historyBefore.size(), std::size_t{2});

    // Move to a fixed amortisation loan at a new rate. The fee and the term
    // belong to the annuity and are gone from the fixed loan.
    QCOMPARE(accounts.updateDebt(id, QStringLiteral("Student loan"),
                                 static_cast<int>(domain::LiabilityKind::StudentLoan),
                                 QStringLiteral("3,5"), accounts.fixedAmortisationStyleIndex(),
                                 QStringLiteral("1500"), QString{}, QString{}),
             QString{});

    QCOMPARE(accounts.debts()->count(), 1);
    const QModelIndex updatedRow = accounts.debts()->index(0, 0);
    QCOMPARE(accounts.debts()->data(updatedRow, DebtRowModel::IdRole).toString(), id);
    QCOMPARE(accounts.debts()->data(updatedRow, DebtRowModel::StyleIndexRole).toInt(),
             accounts.fixedAmortisationStyleIndex());
    QCOMPARE(accounts.debts()->data(updatedRow, DebtRowModel::FeeTextRole).toString(), QString{});
    QCOMPARE(accounts.debts()->data(updatedRow, DebtRowModel::TermYearsTextRole).toString(),
             QString{});

    // Even if a value from the hidden annuity fields reached the view model, a
    // fixed loan ignores it: the style owns which terms exist.
    QCOMPARE(accounts.updateDebt(id, QStringLiteral("Student loan"),
                                 static_cast<int>(domain::LiabilityKind::StudentLoan),
                                 QStringLiteral("3,5"), accounts.fixedAmortisationStyleIndex(),
                                 QStringLiteral("1500"), QStringLiteral("99"), QStringLiteral("5")),
             QString{});

    // The id is the same, so every balance recorded against it is still there
    // and the newest one describes the same loan.
    QCOMPARE(m_controller->debtHistory(id.toStdString()).size(), historyBefore.size());
    const std::optional<domain::Money> latest = m_controller->latestDebtBalance(id.toStdString());
    QVERIFY(latest.has_value());
    QCOMPARE(latest->minorUnits(), std::int64_t{15000000});

    const std::vector<domain::Liability> liabilities = m_controller->liabilities();
    const auto found = std::find_if(liabilities.cbegin(), liabilities.cend(),
                                    [&id](const domain::Liability& liability) {
                                        return QString::fromStdString(liability.id) == id;
                                    });
    QVERIFY(found != liabilities.cend());
    QCOMPARE(found->style, domain::RepaymentStyle::FixedAmortisation);
    QVERIFY(found->monthlyFee.isZero());
    QCOMPARE(found->termMonths, 0);

    QVERIFY(!accounts
                 .updateDebt(QStringLiteral("gone"), QStringLiteral("Loan"),
                             static_cast<int>(domain::LiabilityKind::Other), QStringLiteral("4"),
                             accounts.fixedAmortisationStyleIndex(), QString{}, QString{},
                             QString{})
                 .isEmpty());

    QCOMPARE(accounts.removeDebt(id), QString{});
}

void TestSetupFlow::planShowsTheCostOfTheDebt()
{
    AccountsViewModel accounts;
    QCOMPARE(accounts.addDebt(QStringLiteral("Flat loan"),
                              static_cast<int>(domain::LiabilityKind::Mortgage),
                              QStringLiteral("4,5"), accounts.fixedAmortisationStyleIndex(),
                              QString{}, QString{}, QString{}, QStringLiteral("2000000")),
             QString{});
    const QString id =
        accounts.debts()->data(accounts.debts()->index(0, 0), DebtRowModel::IdRole).toString();

    // A loan with a balance but no terms is named, rather than left as a debt
    // line that never moves without explanation.
    PlanViewModel withoutTerms;
    QVERIFY(withoutTerms.hasDebt());
    QVERIFY(withoutTerms.missingTermsNote().contains(QStringLiteral("Flat loan")));
    QVERIFY(withoutTerms.paymentMismatchNote().isEmpty());

    // An annuity with a term and a payment the term does not imply: interest,
    // fees and the disagreement all become visible.
    QCOMPARE(accounts.updateDebt(
                 id, QStringLiteral("Flat loan"), static_cast<int>(domain::LiabilityKind::Mortgage),
                 QStringLiteral("4,5"), accounts.annuityStyleIndex(), QStringLiteral("5000"),
                 QStringLiteral("50"), QStringLiteral("20")),
             QString{});

    PlanViewModel plan;
    QVERIFY(plan.hasDebt());
    QVERIFY(plan.missingTermsNote().isEmpty());
    QVERIFY(!plan.totalInterestLabel().isEmpty());
    QVERIFY(!plan.totalFeesLabel().isEmpty());
    QVERIFY(plan.costSummary().startsWith(QStringLiteral("Interest over ")));
    QVERIFY(plan.costSummary().contains(QStringLiteral("Fees:")));
    QVERIFY(plan.paymentMismatchNote().startsWith(QStringLiteral("CSN would charge about")));

    QCOMPARE(accounts.removeDebt(id), QString{});
}

void TestSetupFlow::rejectsWhatTheUseCasesReject()
{
    AccountsViewModel accounts;
    accounts.setSelectedBucket(0);
    const QString id = accounts.accounts()
                           ->data(accounts.accounts()->index(0, 0), AccountRowModel::IdRole)
                           .toString();

    QVERIFY(!accounts.addAccount(QString{}, 0, 0).isEmpty());
    QVERIFY(!accounts.recordValuation(id, QStringLiteral("lots")).isEmpty());
    QVERIFY(!accounts.recordValuation(id, QStringLiteral("-100")).isEmpty());
    QVERIFY(!accounts
                 .addDebt(QString{}, static_cast<int>(domain::LiabilityKind::CreditCard),
                          QStringLiteral("19,9"), accounts.fixedAmortisationStyleIndex(), QString{},
                          QString{}, QString{}, QStringLiteral("5000"))
                 .isEmpty());
    QVERIFY(!accounts
                 .addDebt(QStringLiteral("Card"),
                          static_cast<int>(domain::LiabilityKind::CreditCard),
                          QStringLiteral("19,9"), accounts.fixedAmortisationStyleIndex(), QString{},
                          QString{}, QString{}, QString{})
                 .isEmpty());
    QVERIFY(!accounts
                 .addDebt(QStringLiteral("Card"),
                          static_cast<int>(domain::LiabilityKind::CreditCard),
                          QStringLiteral("19,9,9"), accounts.fixedAmortisationStyleIndex(),
                          QString{}, QString{}, QString{}, QStringLiteral("5000"))
                 .isEmpty());

    // None of those failures reached the database.
    QCOMPARE(accounts.debts()->count(), 0);
    QCOMPARE(accounts.accounts()->count(), 1);
}

void TestSetupFlow::cashflowPageRecordsAndChecksTheMonth()
{
    CashflowViewModel cashflow;
    QCOMPARE(cashflow.history()->count(), cashflow.historyMonths());
    QCOMPARE(cashflow.currencyCode(), QStringLiteral("EUR"));

    // The household's month is already recorded, so it is the selected column
    // and the strip does not call it missing.
    QVERIFY(cashflow.selectedMonthRecorded());
    QVERIFY(qAbs(cashflow.needsRatio() - 0.5) < 1e-9);
    QVERIFY(qAbs(cashflow.savingRatio() - 0.2) < 1e-9);

    // Outflows that add up to more than income are refused before they reach
    // the use case: saving is part of income, not something on top of it.
    cashflow.setIncomeText(QStringLiteral("30000"));
    cashflow.setNeedsText(QStringLiteral("20000"));
    cashflow.setWantsText(QStringLiteral("9000"));
    cashflow.setSavingText(QStringLiteral("6000"));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    cashflow.setIncomeText(QString{});
    cashflow.setNeedsText(QStringLiteral("15000"));
    cashflow.setWantsText(QStringLiteral("9000"));
    cashflow.setSavingText(QStringLiteral("6000"));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    // A month that adds up is recorded, and the figures follow it.
    cashflow.setIncomeText(QStringLiteral("30000"));
    cashflow.setNeedsText(QStringLiteral("18000"));
    cashflow.setWantsText(QStringLiteral("9000"));
    cashflow.setSavingText(QStringLiteral("3000"));
    QCOMPARE(cashflow.saveMonth(), QString{});
    QVERIFY(qAbs(cashflow.needsRatio() - 0.6) < 1e-9);
    QVERIFY(qAbs(cashflow.savingRatio() - 0.1) < 1e-9);
    QVERIFY(cashflow.savingsPoints().size() >= 1);

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QCOMPARE(snapshot.facts.flow.needs.minorUnits(), std::int64_t{1800000});
    QCOMPARE(snapshot.facts.flow.saving.minorUnits(), std::int64_t{300000});
}

void TestSetupFlow::cashflowDraftSurvivesReloadAndDiscard()
{
    CashflowViewModel cashflow;
    const QString storedIncome = cashflow.incomeText();
    QVERIFY(!storedIncome.isEmpty());
    QVERIFY(!cashflow.dirty());

    // Typing moves the draft into the view model. A reload - which is what a
    // write from any page triggers - must not throw it away.
    cashflow.setIncomeText(QStringLiteral("31000"));
    QVERIFY(cashflow.dirty());
    cashflow.reload();
    QCOMPARE(cashflow.incomeText(), QStringLiteral("31000"));
    QVERIFY(cashflow.dirty());

    // Discard is the explicit way out, and it restores the saved month.
    cashflow.discardMonth();
    QCOMPARE(cashflow.incomeText(), storedIncome);
    QVERIFY(!cashflow.dirty());
}

void TestSetupFlow::cashflowSavingSplitValidatesAndSaves()
{
    CashflowViewModel cashflow;
    QCOMPARE(cashflow.savingText(), formatMoneyPlain(domain::Money{300000, domain::Currency::Eur}));

    // No loan states repayment terms, so there is nothing to derive an
    // amortisation from and the three parts stay the household's own.
    QVERIFY(!cashflow.amortisationIsDerived());
    QCOMPARE(cashflow.amortisationFromLoans(),
             formatMoneyPlain(domain::Money{0, domain::Currency::Eur}));
    QVERIFY(cashflow.savingSplitValid());

    // One part alone is not a split: the three are all present or all absent.
    cashflow.setBufferSplitText(QStringLiteral("1000"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("Fill all three")));

    // The total is 3 000, so three parts that add to 2 500 leave 500 to place
    // and block the save.
    cashflow.setInvestingSplitText(QStringLiteral("1000"));
    cashflow.setAmortisationSplitText(QStringLiteral("500"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("left to place")));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    cashflow.setAmortisationSplitText(QStringLiteral("1000"));
    QVERIFY(cashflow.savingSplitValid());
    QCOMPARE(cashflow.saveMonth(), QString{});
    QVERIFY(!cashflow.dirty());

    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QVERIFY(snapshot.facts.flow.savingSplit.has_value());
    QCOMPARE(snapshot.facts.flow.savingSplit->toBuffer.minorUnits(), std::int64_t{100000});
    QCOMPARE(snapshot.facts.flow.savingSplit->toInvesting.minorUnits(), std::int64_t{100000});
    QCOMPARE(snapshot.facts.flow.savingSplit->toAmortisation.minorUnits(), std::int64_t{100000});

    // The shares the page shows are the domain's shares of that saving.
    QVERIFY(cashflow.selectedMonthHasSavingSplit());
    QVERIFY(cashflow.bufferSavingShareLabel().startsWith(QStringLiteral("33,3")));
    QCOMPARE(cashflow.bufferSavingAmountLabel(),
             formatMoney(domain::Money{100000, domain::Currency::Eur}));

    // The helper puts the unallocated rest into investments, which is where a
    // household that knows only its buffer and amortisation leaves it.
    cashflow.setBufferSplitText(QStringLiteral("1000"));
    cashflow.setInvestingSplitText(QString{});
    cashflow.setAmortisationSplitText(QStringLiteral("500"));
    QVERIFY(cashflow.savingSplitCanFill());
    cashflow.fillSavingSplitRemainder();
    QCOMPARE(cashflow.investingSplitText(),
             formatMoneyPlain(domain::Money{150000, domain::Currency::Eur}));
    QVERIFY(cashflow.savingSplitValid());

    // More than the saving total is an excess, named in the same words.
    cashflow.setBufferSplitText(QStringLiteral("2000"));
    cashflow.setInvestingSplitText(QStringLiteral("1000"));
    cashflow.setAmortisationSplitText(QStringLiteral("1000"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("too much")));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    // Discard restores the stored breakdown that was saved above.
    cashflow.discardMonth();
    QVERIFY(!cashflow.dirty());
    QVERIFY(cashflow.savingSplitValid());
    QCOMPARE(cashflow.bufferSplitText(),
             formatMoneyPlain(domain::Money{100000, domain::Currency::Eur}));
}

void TestSetupFlow::cashflowDerivesAmortisationFromTheLoanTerms()
{
    // A loan that repays 3 000 kr of principal a month is the month's
    // amortisation. The page shows what the terms repay instead of asking for
    // the same money a second time.
    AccountsViewModel accounts;
    QCOMPARE(accounts.addDebt(
                 QStringLiteral("Mortgage"), static_cast<int>(domain::LiabilityKind::Mortgage),
                 QStringLiteral("4,5"), accounts.fixedAmortisationStyleIndex(),
                 QStringLiteral("3000"), QString{}, QString{}, QStringLiteral("2000000")),
             QString{});
    const QString id =
        accounts.debts()->data(accounts.debts()->index(0, 0), DebtRowModel::IdRole).toString();
    QVERIFY(!id.isEmpty());

    CashflowViewModel cashflow;
    QVERIFY(cashflow.amortisationIsDerived());
    QCOMPARE(cashflow.amortisationFromLoans(),
             formatMoneyPlain(domain::Money{300000, domain::Currency::Eur}));

    // 6 000 kr of saving, of which the loan terms already place 3 000: the
    // buffer and the investments have to fill the rest between them.
    cashflow.setIncomeText(QStringLiteral("30000"));
    cashflow.setNeedsText(QStringLiteral("18000"));
    cashflow.setWantsText(QStringLiteral("6000"));
    cashflow.setSavingText(QStringLiteral("6000"));

    // Adding the typed buffer and investments up alone leaves the amortisation
    // unplaced, which is what the month would have said before.
    cashflow.setBufferSplitText(QStringLiteral("2000"));
    cashflow.setInvestingSplitText(QStringLiteral("1000"));
    QVERIFY(cashflow.savingSplitValid());

    cashflow.setInvestingSplitText(QStringLiteral("500"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("left to place")));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    cashflow.setInvestingSplitText(QStringLiteral("2000"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("too much")));

    // The helper places the rest, with the loans' amortisation already spoken
    // for: 6 000 less 2 000 of buffer and 3 000 of repayment is 1 000.
    cashflow.setInvestingSplitText(QString{});
    QVERIFY(cashflow.savingSplitCanFill());
    cashflow.fillSavingSplitRemainder();
    QCOMPARE(cashflow.investingSplitText(),
             formatMoneyPlain(domain::Money{100000, domain::Currency::Eur}));
    QCOMPARE(cashflow.saveMonth(), QString{});

    // The stored split is the household's two parts and the loans' principal,
    // and it adds up to the saving total.
    const application::DashboardSnapshot saved = m_controller->dashboardSnapshot();
    QCOMPARE(saved.facts.flow.saving.minorUnits(), std::int64_t{600000});
    QVERIFY(saved.facts.flow.savingSplit.has_value());
    QCOMPARE(saved.facts.flow.savingSplit->toBuffer.minorUnits(), std::int64_t{200000});
    QCOMPARE(saved.facts.flow.savingSplit->toInvesting.minorUnits(), std::int64_t{100000});
    QCOMPARE(saved.facts.flow.savingSplit->toAmortisation.minorUnits(), std::int64_t{300000});
    QCOMPARE(saved.facts.flow.savingSplit->total().minorUnits(), std::int64_t{600000});

    // A saving total below what the loan terms repay cannot be split by the
    // model at all: one of the two figures is wrong, and the page says so
    // instead of writing a breakdown that contradicts the loans.
    cashflow.setSavingText(QStringLiteral("2000"));
    QVERIFY(!cashflow.savingSplitValid());
    QVERIFY(cashflow.savingSplitError().contains(QStringLiteral("loan terms")));
    QVERIFY(!cashflow.saveMonth().isEmpty());

    // A month that says nothing about the division keeps saying nothing: with
    // both fields empty the split is absent rather than a split of zeros.
    cashflow.setSavingText(QStringLiteral("6000"));
    cashflow.setBufferSplitText(QString{});
    cashflow.setInvestingSplitText(QString{});
    QVERIFY(cashflow.savingSplitValid());
    QCOMPARE(cashflow.saveMonth(), QString{});
    QVERIFY(!m_controller->dashboardSnapshot().facts.flow.savingSplit.has_value());

    // Removing the loan leaves nothing to derive from, and the month is the
    // household's own three parts again.
    QCOMPARE(accounts.removeDebt(id), QString{});
    cashflow.reload();
    QVERIFY(!cashflow.amortisationIsDerived());
    cashflow.setBufferSplitText(QStringLiteral("2000"));
    cashflow.setInvestingSplitText(QStringLiteral("1000"));
    cashflow.setAmortisationSplitText(QStringLiteral("3000"));
    QVERIFY(cashflow.savingSplitValid());
    QCOMPARE(cashflow.saveMonth(), QString{});
}

void TestSetupFlow::dashboardLeadsWithTheBufferPoint()
{
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    const QVector<BucketRow> rows = bucketRowsOf(snapshot);
    QCOMPARE(rows.size(), 4);

    const auto& target = snapshot.facts.targets[static_cast<std::size_t>(domain::Bucket::Buffer)];
    QVERIFY(target.hasTarget());

    // The buffer's label leads with the chosen point and keeps the band beside
    // it, so a household has something to aim at instead of only a range.
    const QString label = rows.front().targetLabel;
    QVERIFY(label.startsWith(QStringLiteral("Target ")));
    QVERIFY(label.contains(QStringLiteral("(band")));
    QVERIFY(label.contains(formatMoney(target.point)));
    QVERIFY(label.contains(formatMoney(target.band.low)));
    QVERIFY(label.contains(formatMoney(target.band.high)));
}

void TestSetupFlow::planPageProjectsTheHousehold()
{
    PlanViewModel plan;
    QCOMPARE(plan.currencyCode(), QStringLiteral("EUR"));
    QVERIFY(plan.fiTargetKnown());
    QVERIFY(!plan.chartEmpty());
    QVERIFY(!plan.projectionBands().isEmpty());
    QVERIFY(!plan.assumptions().isEmpty());

    // The household is preserving, where the model allocates no free money, so
    // the what-if has no bucket to test and says so rather than pretending.
    QVERIFY(!plan.extraSavingAvailable());
    QVERIFY(!plan.baseYearsLabel().isEmpty());

    // An unreadable amount is reported and does not wipe the chart.
    plan.setExtraMonthlySaving(QStringLiteral("lots"));
    QVERIFY(!plan.extraSavingError().isEmpty());
    QVERIFY(!plan.chartEmpty());

    plan.setExtraMonthlySaving(QStringLiteral("500"));
    QVERIFY(plan.extraSavingError().isEmpty());
    QVERIFY(plan.extraSavingActive());
}

void TestSetupFlow::planDoesNotCountTheHouseAsSpendable()
{
    PlanViewModel withoutHouse;
    QVERIFY(!withoutHouse.hasFiMarker());

    // The flat is worth more than the FI target, but it is not money the
    // household can live off. A projection that started from every bucket
    // would report the target reached in year zero.
    const domain::Asset house = domain::makeAsset("house", "House", domain::AssetKind::RealEstate);
    QVERIFY(m_controller->addAsset(house).ok());
    QVERIFY(m_controller
                ->recordValuation("house", AppController::currentMonth(),
                                  domain::Money{1000000000, domain::Currency::Eur})
                .ok());

    PlanViewModel plan;
    QVERIFY(!plan.chartEmpty());
    QVERIFY(!plan.hasFiMarker());
    QVERIFY(plan.baseYearsLabel().contains(QStringLiteral("Not within")));

    QVERIFY(m_controller->removeAsset("house").ok());
}

void TestSetupFlow::reviewPageAddsAGoalAndProposesNothingAutomatic()
{
    ReviewViewModel review;
    QCOMPARE(review.goals()->count(), 0);
    QVERIFY(review.hasFlow());

    const QString before = review.bufferTargetNowLabel();
    QCOMPARE(review.addGoal(QStringLiteral("Roof"), QStringLiteral("20000"), 5, 1), QString{});
    QCOMPARE(review.goals()->count(), 1);
    // A purchase inside three years moves the buffer target the review reasons
    // about, which is the reason the step exists.
    QVERIFY(review.bufferTargetWithGoalsLabel() != before);

    const QString id =
        review.goals()->data(review.goals()->index(0, 0), GoalRowModel::IdRole).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(review.removeGoal(id), QString{});
    QCOMPARE(review.goals()->count(), 0);

    // The switch changes what the planner is told, and the review page itself
    // never writes a transfer anywhere.
    review.setMarketDown(true);
    QVERIFY(review.marketDown());
    QVERIFY(!review.rationaleLabel().isEmpty());
    QVERIFY(!review.hasNextContribution());
    review.setMarketDown(false);
}

void TestSetupFlow::updateFlowRecordsBothStepsForOneBackfilledMonth()
{
    // A debt as well as an account: one list covering both is the point of the
    // balances step, and a debt could not be updated from a page before it.
    AccountsViewModel accounts;
    accounts.setSelectedBucket(static_cast<int>(domain::Bucket::Buffer));
    QCOMPARE(accounts.addAccount(QStringLiteral("Update flow account"),
                                 static_cast<int>(domain::AssetKind::BankAccount),
                                 static_cast<int>(domain::Bucket::Buffer)),
             QString{});
    QCOMPARE(accounts.addDebt(QStringLiteral("Update flow loan"),
                              static_cast<int>(domain::LiabilityKind::StudentLoan),
                              QStringLiteral("2,5"), accounts.fixedAmortisationStyleIndex(),
                              QString{}, QString{}, QString{}, QStringLiteral("40000")),
             QString{});

    const auto idOf = [this](const QString& name) {
        for (const domain::Asset& asset : m_controller->assets()) {
            if (QString::fromStdString(asset.name) == name)
                return asset.id;
        }
        return std::string{};
    };
    const std::string accountId = idOf(QStringLiteral("Update flow account"));
    QVERIFY(!accountId.empty());

    UpdateFlowViewModel flow;
    flow.begin();

    QCOMPARE(flow.monthIndex(), 0);
    QVERIFY(flow.editingCurrentMonth());
    QCOMPARE(flow.balancesStatus(), QStringLiteral("Not updated"));
    QCOMPARE(flow.cashflowStatus(), QStringLiteral("Not updated"));
    QVERIFY(flow.hasAccounts());
    QVERIFY(!flow.dirty());

    const auto rowOf = [&flow](const QString& name) {
        for (int row = 0; row < flow.balances()->count(); ++row) {
            const QModelIndex at = flow.balances()->index(row, 0);
            if (flow.balances()->data(at, BalanceEntryModel::NameRole).toString() == name)
                return row;
        }
        return -1;
    };
    const auto textOf = [&flow](int row, int role) {
        return flow.balances()->data(flow.balances()->index(row, 0), role).toString();
    };

    const int accountRow = rowOf(QStringLiteral("Update flow account"));
    const int debtRow = rowOf(QStringLiteral("Update flow loan"));
    QVERIFY(accountRow >= 0);
    QVERIFY(debtRow >= 0);

    // Never valued, the field starts empty and the row says so rather than
    // starting from a zero the record does not hold.
    QCOMPARE(textOf(accountRow, BalanceEntryModel::AmountTextRole), QString{});
    QCOMPARE(textOf(accountRow, BalanceEntryModel::RecordLabelRole),
             QStringLiteral("Never recorded"));

    // Backfill: three months back, both steps, in one pass.
    const domain::YearMonth current = AppController::currentMonth();
    const domain::YearMonth backfilled = current.plusMonths(-3);
    flow.setMonthIndex(3);
    QCOMPARE(flow.monthLabel(), formatMonth(backfilled, QStringLiteral("MMMM yyyy")));
    QVERIFY(!flow.monthNote().isEmpty());

    flow.balances()->setAmountText(accountRow, QStringLiteral("5000"));
    flow.balances()->setAmountText(debtRow, QStringLiteral("38000"));
    QVERIFY(flow.dirty());
    QCOMPARE(flow.balancesStatus(), QStringLiteral("Unsaved changes"));

    QCOMPARE(flow.saveBalances(), QString{});
    QCOMPARE(flow.balancesStatus(), QStringLiteral("Saved"));
    // The other step was not touched, and does not claim to have been.
    QCOMPARE(flow.cashflowStatus(), QStringLiteral("Not updated"));
    QVERIFY(!flow.dirty());

    // Written under the month that was chosen, not under today.
    const std::optional<domain::Money> savedBalance =
        m_controller->assetBalanceAt(accountId, backfilled);
    QVERIFY(savedBalance.has_value());
    QCOMPARE(savedBalance->minorUnits(), std::int64_t{500000});

    flow.cashflow()->setIncomeText(QStringLiteral("20000"));
    flow.cashflow()->setNeedsText(QStringLiteral("10000"));
    flow.cashflow()->setWantsText(QStringLiteral("5000"));
    flow.cashflow()->setSavingText(QStringLiteral("5000"));
    QCOMPARE(flow.saveCashflow(), QString{});
    QCOMPARE(flow.cashflowStatus(), QStringLiteral("Saved"));

    const std::optional<domain::MonthFlow> backfilledFlow = m_controller->monthFlow(backfilled);
    QVERIFY(backfilledFlow.has_value());
    QCOMPARE(backfilledFlow->income.minorUnits(), std::int64_t{2000000});

    // Backfilling added a month to the record; it did not move it. The
    // dashboard still describes the most recent month, and the flow says so.
    const application::DashboardSnapshot snapshot = m_controller->dashboardSnapshot();
    QVERIFY(snapshot.latestBalanceMonth.has_value());
    QCOMPARE(QString::fromStdString(snapshot.latestBalanceMonth->toString()),
             QString::fromStdString(current.toString()));
    QVERIFY(flow.resultNote().contains(formatMonth(current, QStringLiteral("MMMM yyyy"))));

    // A later month starts from the figure the record already implies, and
    // says that it is a carry-forward rather than a fresh reading.
    flow.setMonthIndex(2);
    QCOMPARE(flow.balancesStatus(), QStringLiteral("Not updated"));
    // Changing the month after a save says so: the month on screen is not the
    // one that was written.
    QVERIFY(flow.monthNote().contains(formatMonth(backfilled, QStringLiteral("MMMM yyyy"))));
    const int carriedRow = rowOf(QStringLiteral("Update flow account"));
    QVERIFY(carriedRow >= 0);
    QCOMPARE(textOf(carriedRow, BalanceEntryModel::AmountTextRole),
             formatMoneyPlain(domain::Money{500000, domain::Currency::Eur}));
    QCOMPARE(textOf(carriedRow, BalanceEntryModel::NoteRole),
             QStringLiteral("Unchanged from %1.")
                 .arg(formatMonth(backfilled, QStringLiteral("MMMM yyyy"))));
    QVERIFY(!flow.dirty());

    // A field that cannot be read stops the whole step: the month is either
    // recorded or it is not.
    flow.balances()->setAmountText(carriedRow, QStringLiteral("5000 kr"));
    QVERIFY(!textOf(carriedRow, BalanceEntryModel::ErrorRole).isEmpty());
    QVERIFY(!flow.saveBalances().isEmpty());
    QVERIFY(!m_controller->assetBalanceAt(accountId, current.plusMonths(-2)).has_value());

    // Step 2 refuses the same month for the same reason the page does.
    flow.cashflow()->setIncomeText(QStringLiteral("1000"));
    flow.cashflow()->setNeedsText(QStringLiteral("900"));
    flow.cashflow()->setWantsText(QStringLiteral("900"));
    QVERIFY(!flow.saveCashflow().isEmpty());
    QCOMPARE(flow.cashflowStatus(), QStringLiteral("Unsaved changes"));
}

QTEST_GUILESS_MAIN(TestSetupFlow)

#include "tst_setup_flow.moc"
