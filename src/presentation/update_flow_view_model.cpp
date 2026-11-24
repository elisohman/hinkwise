// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "update_flow_view_model.h"

#include "app_controller.h"
#include "application/update_flow.h"
#include "catalog.h"
#include "input_text.h"

#include <QCoreApplication>
#include <QString>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

const QString kMonthFormat = QStringLiteral("MMMM yyyy");

/// "asset:<id>" and "debt:<id>". The two tables number their rows separately,
/// so a row id says which one it belongs to and the save never has to guess.
const QString kAssetPrefix = QStringLiteral("asset:");
const QString kDebtPrefix = QStringLiteral("debt:");

/// The later of two recorded months, ignoring whichever of them is absent.
std::optional<domain::YearMonth> laterOf(std::optional<domain::YearMonth> first,
                                         std::optional<domain::YearMonth> second)
{
    if (!first)
        return second;
    if (!second)
        return first;
    return *second < *first ? first : second;
}

/// The value the record already implies, as the note under the field puts it.
QString carriedNote(const application::MoneyPoint& effect, domain::YearMonth month)
{
    if (!(effect.month < month))
        return {};
    return UpdateFlowViewModel::tr("Unchanged from %1.")
        .arg(formatMonth(effect.month, kMonthFormat));
}

} // namespace

// ---------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------

BalanceEntryModel::BalanceEntryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BalanceEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant BalanceEntryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const BalanceEntryRow& row = m_rows.at(index.row());
    switch (role) {
    case NameRole:
        return QVariant{row.name};
    case DetailLabelRole:
        return QVariant{row.detailLabel};
    case IsDebtRole:
        return QVariant{row.isDebt};
    case RecordLabelRole:
        return QVariant{row.recordLabel};
    case AmountTextRole:
        return QVariant{row.amountText};
    case NoteRole:
        return QVariant{row.note};
    case ErrorRole:
        return QVariant{row.error};
    case DirtyRole:
        return QVariant{row.dirty};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> BalanceEntryModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {DetailLabelRole, "detailLabel"},
        {IsDebtRole, "isDebt"},
        {RecordLabelRole, "recordLabel"},
        {AmountTextRole, "amountText"},
        {NoteRole, "note"},
        {ErrorRole, "fieldError"},
        {DirtyRole, "dirty"},
    };
}

void BalanceEntryModel::setRows(QVector<BalanceEntryRow> rows, domain::YearMonth month,
                                domain::Currency currency)
{
    m_month = month;
    m_currency = currency;
    for (BalanceEntryRow& row : rows)
        applyReading(row);

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
    emit edited();
}

void BalanceEntryModel::setAmountText(int row, const QString& text)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).amountText == text)
        return;

    BalanceEntryRow& entry = m_rows[row];
    entry.amountText = text;
    entry.dirty = entry.amountText != entry.recordedText;
    applyReading(entry);

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {AmountTextRole, NoteRole, ErrorRole, DirtyRole});
    emit edited();
}

void BalanceEntryModel::applyReading(BalanceEntryRow& row) const
{
    row.error.clear();
    row.note.clear();

    // An empty field says nothing about the month: the row is left out of the
    // save rather than recorded as zero, which would be a claim it cannot make.
    if (row.amountText.trimmed().isEmpty())
        return;

    const MoneyInput parsed = parseMoneyInput(row.amountText, m_currency);
    if (!parsed.ok) {
        row.error = parsed.error;
        return;
    }
    if (row.effect.has_value() && parsed.value == row.effect->amount)
        row.note = carriedNote(*row.effect, m_month);
}

bool BalanceEntryModel::dirty() const
{
    return std::any_of(m_rows.cbegin(), m_rows.cend(),
                       [](const BalanceEntryRow& row) { return row.dirty; });
}

// ---------------------------------------------------------------------------
// The flow
// ---------------------------------------------------------------------------

UpdateFlowViewModel::UpdateFlowViewModel(QObject* parent)
    : QObject(parent)
    , m_balances(new BalanceEntryModel(this))
    , m_cashflow(new CashflowViewModel(this))
{
    // The status of each step follows the draft inside it, and the
    // leave-confirmation reads both drafts through dirty().
    connect(m_balances, &BalanceEntryModel::edited, this, &UpdateFlowViewModel::changed);
    connect(m_cashflow, &CashflowViewModel::changed, this, &UpdateFlowViewModel::changed);
    rebuildMonthLabels();
    reload();
}

void UpdateFlowViewModel::rebuildMonthLabels()
{
    m_monthLabels.clear();
    const std::vector<domain::YearMonth> months = application::recordableMonths(
        AppController::currentMonth(), application::kRecordableMonths);
    m_monthLabels.reserve(static_cast<qsizetype>(months.size()));
    for (const domain::YearMonth& month : months)
        m_monthLabels.append(formatMonth(month, kMonthFormat));
}

domain::YearMonth UpdateFlowViewModel::month() const
{
    return AppController::currentMonth().plusMonths(-m_monthIndex);
}

QString UpdateFlowViewModel::monthLabel() const
{
    return formatMonth(month(), kMonthFormat);
}

bool UpdateFlowViewModel::editingCurrentMonth() const
{
    return m_monthIndex == 0;
}

void UpdateFlowViewModel::begin()
{
    m_monthIndex = 0;
    m_lastSavedMonth.reset();
    m_balancesSavedForMonth = false;
    m_cashflowSavedForMonth = false;
    m_balancesError.clear();
    m_resultNote.clear();
    rebuildMonthLabels();
    reload();
}

void UpdateFlowViewModel::setMonthIndex(int index)
{
    const int last = std::max(0, static_cast<int>(m_monthLabels.size()) - 1);
    const int clamped = std::clamp(index, 0, last);
    if (clamped == m_monthIndex)
        return;

    m_monthIndex = clamped;
    // A step saved for one month says nothing about another, and a note about
    // what a save did belongs to the month it was saved for.
    m_balancesSavedForMonth = false;
    m_cashflowSavedForMonth = false;
    m_balancesError.clear();
    m_resultNote.clear();
    reload();
}

void UpdateFlowViewModel::reload()
{
    AppController* controller = AppController::instance();
    const application::DashboardSnapshot snapshot =
        controller != nullptr ? controller->dashboardSnapshot() : application::DashboardSnapshot{};
    m_currency = snapshot.profile.baseCurrency;
    m_actionLabel = snapshot.profile.trackingMode == domain::TrackingMode::Planning
                        ? tr("Update budget")
                        : tr("Update finances");

    // What the chosen month means, in one sentence: a month already saved in
    // this visit is a different one, a past month is a backfill, and the
    // current month needs no explanation.
    const domain::YearMonth chosen = month();
    if (m_lastSavedMonth.has_value() && *m_lastSavedMonth != chosen) {
        m_monthNote = tr("You saved %1 earlier in this visit. %2 is a different month, so "
                         "nothing here is saved yet.")
                          .arg(formatMonth(*m_lastSavedMonth, kMonthFormat), monthLabel());
    } else if (chosen == AppController::currentMonth()) {
        m_monthNote.clear();
    } else {
        m_monthNote = tr("You are editing %1, a past month. The overview keeps showing your "
                         "most recent figures.")
                          .arg(monthLabel());
    }

    // Step 2 follows the same month as step 1, so one pass records both for the
    // month the household chose.
    m_cashflow->setEditingMonth(chosen);

    // What the row says about the record, which has to be true about the month
    // on screen and not about the newest month there is: backfilling a month
    // older than every recording has no value in effect, and saying "last
    // recorded November" would name a month that had not happened yet.
    const auto recordLabel = [&chosen](const std::optional<application::MoneyPoint>& effect,
                                       const std::optional<application::MoneyPoint>& latest) {
        if (effect.has_value()) {
            return UpdateFlowViewModel::tr("Last recorded %1 · %2")
                .arg(formatMoney(effect->amount), formatMonth(effect->month, kMonthFormat));
        }
        if (latest.has_value()) {
            return UpdateFlowViewModel::tr("Nothing recorded before %1")
                .arg(formatMonth(chosen, kMonthFormat));
        }
        return UpdateFlowViewModel::tr("Never recorded");
    };

    QVector<BalanceEntryRow> rows;
    if (controller != nullptr) {
        const std::vector<domain::Asset> assets = controller->assets();
        const std::vector<domain::Liability> liabilities = controller->liabilities();
        rows.reserve(static_cast<qsizetype>(assets.size() + liabilities.size()));

        for (const domain::Asset& asset : assets) {
            const std::vector<application::MoneyPoint> history = controller->assetHistory(asset.id);

            BalanceEntryRow row;
            row.id = kAssetPrefix + QString::fromStdString(asset.id);
            row.name = QString::fromStdString(asset.name);
            row.detailLabel = tr("%1 · %2").arg(catalog::assetKindLabel(asset.kind),
                                                catalog::bucketLabel(asset.bucket));
            row.effect =
                history.empty()
                    ? std::nullopt
                    : application::amountInEffectAt(
                          std::span<const application::MoneyPoint>{history.data(), history.size()},
                          chosen);
            row.recordLabel = recordLabel(
                row.effect, history.empty()
                                ? std::nullopt
                                : std::optional<application::MoneyPoint>{history.back()});
            // The field starts from the figure the record already implies for
            // the month, never from a later month's: recording January after
            // March must not carry March's balance back into it.
            row.recordedText = row.effect ? formatMoneyPlain(row.effect->amount) : QString{};
            row.amountText = row.recordedText;
            rows.append(std::move(row));
        }

        for (const domain::Liability& liability : liabilities) {
            const std::vector<application::MoneyPoint> history =
                controller->debtHistory(liability.id);

            BalanceEntryRow row;
            row.id = kDebtPrefix + QString::fromStdString(liability.id);
            row.isDebt = true;
            row.name = QString::fromStdString(liability.name);
            row.detailLabel = tr("%1 · %2").arg(catalog::liabilityKindLabel(liability.kind),
                                                formatPercentage(liability.interestRate));
            row.effect =
                history.empty()
                    ? std::nullopt
                    : application::amountInEffectAt(
                          std::span<const application::MoneyPoint>{history.data(), history.size()},
                          chosen);
            row.recordLabel = recordLabel(
                row.effect, history.empty()
                                ? std::nullopt
                                : std::optional<application::MoneyPoint>{history.back()});
            row.recordedText = row.effect ? formatMoneyPlain(row.effect->amount) : QString{};
            row.amountText = row.recordedText;
            rows.append(std::move(row));
        }
    }

    m_hasAccounts = !rows.isEmpty();
    m_balances->setRows(std::move(rows), chosen, m_currency);
    emit changed();
}

QString UpdateFlowViewModel::balancesStatus() const
{
    if (m_balances->dirty())
        return tr("Unsaved changes");
    return m_balancesSavedForMonth ? tr("Saved") : tr("Not updated");
}

QString UpdateFlowViewModel::cashflowStatus() const
{
    if (m_cashflow->dirty())
        return tr("Unsaved changes");
    return m_cashflowSavedForMonth ? tr("Saved") : tr("Not updated");
}

QString UpdateFlowViewModel::balancesStatusKey() const
{
    if (m_balances->dirty())
        return QStringLiteral("unsaved");
    return m_balancesSavedForMonth ? QStringLiteral("saved") : QStringLiteral("pending");
}

QString UpdateFlowViewModel::cashflowStatusKey() const
{
    if (m_cashflow->dirty())
        return QStringLiteral("unsaved");
    return m_cashflowSavedForMonth ? QStringLiteral("saved") : QStringLiteral("pending");
}

bool UpdateFlowViewModel::dirty() const
{
    return m_balances->dirty() || m_cashflow->dirty();
}

QString UpdateFlowViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_currency));
}

QString UpdateFlowViewModel::saveBalances()
{
    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_balancesError = tr("The database is not open.");
        emit changed();
        return m_balancesError;
    }

    const domain::YearMonth chosen = month();

    // Everything is read before anything is written: a typo in the third field
    // must not leave the first two recorded and the month half done.
    struct Pending {
        bool isDebt = false;
        std::string id;
        domain::Money amount;
    };
    std::vector<Pending> pending;
    for (const BalanceEntryRow& row : m_balances->rows()) {
        if (row.amountText.trimmed().isEmpty())
            continue;
        const MoneyInput parsed = parseMoneyInput(row.amountText, m_currency);
        if (!parsed.ok) {
            m_balancesError = tr("%1: %2").arg(row.name, parsed.error);
            emit changed();
            return m_balancesError;
        }
        const bool isDebt = row.id.startsWith(kDebtPrefix);
        const QString id = row.id.mid(isDebt ? kDebtPrefix.size() : kAssetPrefix.size());
        pending.push_back(Pending{isDebt, id.toStdString(), parsed.value});
    }

    if (pending.empty()) {
        m_balancesError = tr("Enter at least one balance to save.");
        emit changed();
        return m_balancesError;
    }

    // One month of balances is one decision, so the rows are recorded inside one
    // transaction: it is committed once every row succeeded, and dropped
    // uncommitted - which rolls the month back - on the first row that fails.
    application::Result<std::unique_ptr<application::Transaction>> begun =
        controller->beginTransaction();
    if (!begun.ok()) {
        m_balancesError = QString::fromStdString(begun.error());
        emit changed();
        return m_balancesError;
    }
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    for (const Pending& entry : pending) {
        const application::Status status =
            entry.isDebt ? controller->recordDebtBalance(entry.id, chosen, entry.amount)
                         : controller->recordValuation(entry.id, chosen, entry.amount);
        if (!status.ok()) {
            transaction.reset();
            m_balancesError = QString::fromStdString(status.error());
            emit changed();
            return m_balancesError;
        }
    }

    const application::Status committed = transaction->commit();
    if (!committed.ok()) {
        m_balancesError = QString::fromStdString(committed.error());
        emit changed();
        return m_balancesError;
    }

    m_balancesError.clear();
    m_balancesSavedForMonth = true;
    m_lastSavedMonth = chosen;
    // The rows are read again so the "last recorded" line follows the write
    // that just happened, and the fields now start from what is stored.
    reload();
    noteSaveResult(chosen);
    emit changed();
    return {};
}

QString UpdateFlowViewModel::saveCashflow()
{
    const domain::YearMonth chosen = month();
    const QString failure = m_cashflow->saveMonth();
    if (!failure.isEmpty())
        return failure;

    m_cashflowSavedForMonth = true;
    m_lastSavedMonth = chosen;
    noteSaveResult(chosen);
    emit changed();
    return {};
}

void UpdateFlowViewModel::noteSaveResult(domain::YearMonth savedMonth)
{
    // Only a past month has anything to explain. The overview describes the
    // most recent month, and backfilling must never look like moving it.
    m_resultNote.clear();
    if (savedMonth == AppController::currentMonth())
        return;

    const AppController* controller = AppController::instance();
    if (controller == nullptr)
        return;

    const application::DashboardSnapshot snapshot = controller->dashboardSnapshot();
    const std::optional<domain::YearMonth> latest =
        laterOf(snapshot.latestBalanceMonth, snapshot.latestFlowMonth);
    if (latest.has_value() && savedMonth < *latest) {
        m_resultNote =
            tr("Your most recent figures are still %1.").arg(formatMonth(*latest, kMonthFormat));
        return;
    }
    m_resultNote =
        tr("%1 is now the most recent month on record.").arg(formatMonth(savedMonth, kMonthFormat));
}

} // namespace hinkwise::presentation
