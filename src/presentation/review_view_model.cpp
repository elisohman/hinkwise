// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "review_view_model.h"

#include "app_controller.h"
#include "catalog.h"
#include "domain/phase.h"
#include "domain/rebalance.h"
#include "domain/targets.h"
#include "input_text.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <QUuid>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

/// How far ahead the buffer target counts a planned purchase. The model's
/// three years, as a month count rather than a date so no calendar is needed.
constexpr int kBufferHorizonMonths = 36;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString bandLabel(const domain::ResolvedTarget& target)
{
    if (!target.hasTarget())
        return ReviewViewModel::tr("No target in this phase");
    return ReviewViewModel::tr("%1 – %2").arg(formatMoney(target.band.low),
                                              formatMoney(target.band.high));
}

/// What is left of income after needs, wants and saving. The planner only says
/// where money goes, so a deficit is reported as nothing to allocate.
domain::Money surplusOf(const domain::MonthFlow& flow)
{
    const domain::Money surplus = flow.income - flow.needs - flow.wants - flow.saving;
    return surplus.isPositive() ? surplus : domain::Money::zero(flow.income.currency());
}

} // namespace

// ---------------------------------------------------------------------------
// Planned purchases
// ---------------------------------------------------------------------------

GoalRowModel::GoalRowModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int GoalRowModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant GoalRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const GoalRow& row = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return QVariant{row.id};
    case NameRole:
        return QVariant{row.name};
    case AmountLabelRole:
        return QVariant{row.amountLabel};
    case DueLabelRole:
        return QVariant{row.dueLabel};
    case CountedRole:
        return QVariant{row.counted};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> GoalRowModel::roleNames() const
{
    return {
        {IdRole, "goalId"},         {NameRole, "name"},       {AmountLabelRole, "amountLabel"},
        {DueLabelRole, "dueLabel"}, {CountedRole, "counted"},
    };
}

void GoalRowModel::setRows(QVector<GoalRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

TransferRowModel::TransferRowModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TransferRowModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TransferRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const TransferRow& row = m_rows.at(index.row());
    switch (role) {
    case FromLabelRole:
        return QVariant{row.fromLabel};
    case ToLabelRole:
        return QVariant{row.toLabel};
    case FromColourKeyRole:
        return QVariant{row.fromColourKey};
    case ToColourKeyRole:
        return QVariant{row.toColourKey};
    case AmountLabelRole:
        return QVariant{row.amountLabel};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> TransferRowModel::roleNames() const
{
    return {
        {FromLabelRole, "fromLabel"},         {ToLabelRole, "toLabel"},
        {FromColourKeyRole, "fromColourKey"}, {ToColourKeyRole, "toColourKey"},
        {AmountLabelRole, "amountLabel"},
    };
}

void TransferRowModel::setRows(QVector<TransferRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------
// The year-end review
// ---------------------------------------------------------------------------

ReviewViewModel::ReviewViewModel(QObject* parent)
    : QObject(parent)
    , m_goals(new GoalRowModel(this))
    , m_transfers(new TransferRowModel(this))
    , m_buckets(new BucketsModel(this))
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &ReviewViewModel::reload);
    }
    reload();
}

QString ReviewViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_currency));
}

QStringList ReviewViewModel::monthLabels() const
{
    QStringList labels;
    const QLocale locale;
    for (int month = 1; month <= 12; ++month)
        labels.append(locale.monthName(month, QLocale::LongFormat));
    return labels;
}

int ReviewViewModel::dueMonthDefault() const
{
    return static_cast<int>(AppController::currentMonth().month()) - 1;
}

int ReviewViewModel::dueYearDefault() const
{
    // The list always starts at the current year, so its first entry is the
    // default whenever the current year is one of the choices.
    return 0;
}

void ReviewViewModel::reload()
{
    AppController* controller = AppController::instance();
    apply(controller != nullptr ? controller->dashboardSnapshot()
                                : application::DashboardSnapshot{});
}

void ReviewViewModel::apply(const application::DashboardSnapshot& snapshot)
{
    m_hasProfile = snapshot.hasProfile;
    m_currency = snapshot.profile.baseCurrency;

    // Step 1: the situation the household declared, and where each part of it
    // is edited. Nothing here changes it: the review's job is to notice.
    m_phaseLabel = QString::fromUtf8(domain::labelOf(snapshot.profile.phase));
    m_marketDownApplies = !domain::isAccumulating(snapshot.profile.phase);
    m_phaseNote =
        tr("You chose this when the household was set up. The Settings page will let you change "
           "it when your situation does.");
    m_hasFlow = snapshot.hasFlow;
    m_incomeLabel = m_hasFlow ? formatMoney(snapshot.facts.flow.income) : tr("Not recorded");
    m_expensesLabel =
        m_hasFlow ? formatMoney(snapshot.metrics.monthlyExpenses) : tr("Not recorded");
    m_flowNote =
        m_hasFlow
            ? tr("Income and the needs and wants it covers. Record a new month on the Cashflow "
                 "page.")
            : tr("No month has been recorded yet. Income and spending go on the Cashflow page.");

    // Step 2: the planned purchases the buffer target counts.
    const int currentYear = QDate::currentDate().year();
    m_dueYearLabels.clear();
    for (int offset = 0; offset <= 3; ++offset)
        m_dueYearLabels.append(QString::number(currentYear + offset));

    const domain::YearMonth current = AppController::currentMonth();
    const domain::YearMonth horizon = current.plusMonths(kBufferHorizonMonths);

    domain::Money lumpSums = domain::Money::zero(m_currency);
    QVector<GoalRow> goals;
    m_goalsBeyondThreeYears = false;
    if (AppController* controller = AppController::instance()) {
        const QLocale locale;
        for (const domain::PlannedExpense& expense : controller->plannedExpenses()) {
            if (expense.amount.currency() != m_currency)
                continue;

            const bool counted = !(horizon < expense.dueMonth);
            if (counted)
                lumpSums += expense.amount;
            else
                m_goalsBeyondThreeYears = true;

            goals.append(GoalRow{
                QString::fromStdString(expense.id),
                QString::fromStdString(expense.name),
                formatMoney(expense.amount),
                locale.toString(
                    QDate{expense.dueMonth.year(), static_cast<int>(expense.dueMonth.month()), 1},
                    QStringLiteral("MMMM yyyy")),
                counted,
            });
        }
    }
    m_goals->setRows(std::move(goals));
    m_lumpSumLabel = formatMoney(lumpSums);

    // The buffer target the model states, and the one it states once the
    // purchases inside three years are counted. Both come from the domain's
    // own resolve(); nothing here sizes a bucket by hand.
    const domain::ResolvedTarget& bufferNow =
        snapshot.facts.targets[static_cast<std::size_t>(domain::Bucket::Buffer)];
    domain::TargetInputs inputs{m_currency};
    inputs.monthlyExpenses = snapshot.metrics.monthlyExpenses;
    inputs.annualExpenses = snapshot.metrics.annualExpenses;
    inputs.lumpSumsNextThreeYears = lumpSums;
    inputs.growthBalance =
        snapshot.facts.sheet.perBucket[static_cast<std::size_t>(domain::Bucket::Growth)];
    inputs.riskTolerance = snapshot.profile.riskTolerance;

    application::DashboardSnapshot reviewed = snapshot;
    reviewed.facts.targets =
        domain::resolve(domain::policyFor(snapshot.profile.phase).targets(), inputs);
    const domain::ResolvedTarget& bufferWithGoals =
        reviewed.facts.targets[static_cast<std::size_t>(domain::Bucket::Buffer)];

    m_bufferTargetNowLabel = bandLabel(bufferNow);
    m_bufferTargetWithGoalsLabel = bandLabel(bufferWithGoals);
    if (!lumpSums.isPositive())
        m_goalsEffectLabel =
            tr("No purchase inside three years, so the buffer target is unchanged.");
    else
        m_goalsEffectLabel =
            tr("Purchases inside three years add %1 to the buffer target.").arg(m_lumpSumLabel);

    // Step 3: the four buckets against the bands the review has just been
    // reasoning about, which is why the rows are built from `reviewed`.
    m_buckets->setRows(bucketRowsOf(reviewed));

    // Step 4: the proposal, with the switch the household can see the effect
    // of. The planner owns the rules; this only fills in its input.
    domain::RebalanceInput input;
    input.phase = snapshot.profile.phase;
    input.balances = snapshot.facts.sheet.perBucket;
    input.targets = reviewed.facts.targets;
    input.monthlySurplus = surplusOf(snapshot.facts.flow);
    input.marketDown = m_marketDown;
    const domain::RebalanceProposal proposal = domain::planRebalance(input);

    m_hasNextContribution = proposal.nextContribution.has_value();
    m_nextContributionLabel = m_hasNextContribution
                                  ? catalog::bucketLabel(*proposal.nextContribution)
                                  : tr("No free money to allocate in this phase");
    m_rationaleLabel = QString::fromStdString(proposal.rationale);

    QVector<TransferRow> transfers;
    transfers.reserve(static_cast<qsizetype>(proposal.transfers.size()));
    for (const domain::Transfer& transfer : proposal.transfers) {
        transfers.append(TransferRow{
            catalog::bucketLabel(transfer.from),
            catalog::bucketLabel(transfer.to),
            catalog::bucketColourKey(transfer.from),
            catalog::bucketColourKey(transfer.to),
            formatMoney(transfer.amount),
        });
    }
    m_transfers->setRows(std::move(transfers));

    emit changed();
}

void ReviewViewModel::setMarketDown(bool marketDown)
{
    if (marketDown == m_marketDown)
        return;
    m_marketDown = marketDown;
    reload();
}

QString ReviewViewModel::addGoal(const QString& name, const QString& amountText, int monthIndex,
                                 int yearIndex)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Give the purchase a name.");

    const MoneyInput amount = parseMoneyInput(amountText, m_currency);
    if (!amount.ok)
        return amount.error;
    if (!amount.value.isPositive())
        return tr("A planned purchase needs an amount above zero.");

    if (m_dueYearLabels.isEmpty())
        return tr("The years to choose from are not known yet. Reload the page.");
    const int year = QDate::currentDate().year() +
                     std::clamp(yearIndex, 0, static_cast<int>(m_dueYearLabels.size()) - 1);
    const unsigned month = static_cast<unsigned>(std::clamp(monthIndex, 0, 11)) + 1;

    domain::PlannedExpense expense;
    expense.id = newId().toStdString();
    expense.name = trimmed.toStdString();
    expense.amount = amount.value;
    expense.dueMonth = domain::YearMonth{year, month};

    const application::Status status = controller->addPlannedExpense(expense);
    return status.ok() ? QString{} : QString::fromStdString(status.error());
}

QString ReviewViewModel::removeGoal(const QString& goalId)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const application::Status status = controller->removePlannedExpense(goalId.toStdString());
    return status.ok() ? QString{} : QString::fromStdString(status.error());
}

} // namespace hinkwise::presentation
