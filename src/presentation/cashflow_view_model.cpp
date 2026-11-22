// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cashflow_view_model.h"

#include "app_controller.h"
#include "input_text.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

QString percentLabel(domain::Percentage percentage, int decimals)
{
    const QLocale locale;
    return CashflowViewModel::tr("%1 %").arg(
        locale.toString(static_cast<double>(percentage.basisPoints()) / 100.0, 'f', decimals));
}

} // namespace

// ---------------------------------------------------------------------------
// History columns
// ---------------------------------------------------------------------------

HistoryColumnsModel::HistoryColumnsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int HistoryColumnsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant HistoryColumnsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const HistoryColumn& column = m_rows.at(index.row());
    switch (role) {
    case LabelRole:
        return QVariant{column.label};
    case FullLabelRole:
        return QVariant{column.fullLabel};
    case RecordedRole:
        return QVariant{column.recorded};
    case NeedsRatioRole:
        return QVariant{column.needsRatio};
    case WantsRatioRole:
        return QVariant{column.wantsRatio};
    case SavingRatioRole:
        return QVariant{column.savingRatio};
    case SelectedRole:
        return QVariant{column.selected};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> HistoryColumnsModel::roleNames() const
{
    return {
        {LabelRole, "label"},           {FullLabelRole, "fullLabel"},
        {RecordedRole, "recorded"},     {NeedsRatioRole, "needsRatio"},
        {WantsRatioRole, "wantsRatio"}, {SavingRatioRole, "savingRatio"},
        {SelectedRole, "selected"},
    };
}

void HistoryColumnsModel::setRows(QVector<HistoryColumn> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

void HistoryColumnsModel::setSelected(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    for (int index = 0; index < m_rows.size(); ++index)
        m_rows[index].selected = index == row;
    emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), {SelectedRole});
}

// ---------------------------------------------------------------------------
// Cashflow page
// ---------------------------------------------------------------------------

CashflowViewModel::CashflowViewModel(QObject* parent)
    : QObject(parent)
    , m_history(new HistoryColumnsModel(this))
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &CashflowViewModel::reload);
    }
    reload();
}

QString CashflowViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_currency));
}

QString CashflowViewModel::monthLabel(domain::YearMonth month, const QString& format) const
{
    const QLocale locale;
    const QDate date{month.year(), static_cast<int>(month.month()), 1};
    return locale.toString(date, format);
}

void CashflowViewModel::reload()
{
    AppController* controller = AppController::instance();
    const application::DashboardSnapshot snapshot =
        controller != nullptr ? controller->dashboardSnapshot() : application::DashboardSnapshot{};
    m_hasProfile = snapshot.hasProfile;
    m_currency = snapshot.profile.baseCurrency;

    const domain::YearMonth current = AppController::currentMonth();
    m_currentMonthLabel = monthLabel(current, QStringLiteral("MMMM yyyy"));

    const std::vector<application::MonthFlowRecord> records =
        controller != nullptr ? controller->recentFlows(kHistoryMonths)
                              : std::vector<application::MonthFlowRecord>{};
    const QVector<FlowMonth> months = flowMonthsOf(records, current, kHistoryMonths, m_currency);

    // The selection survives a reload while it is still inside the window, so
    // recording the current month does not jump the strip back to it.
    const domain::YearMonth first = months.isEmpty() ? current : months.front().month;
    if (m_selectedMonth < first || current < m_selectedMonth)
        m_selectedMonth = current;

    // The editable fields always describe the current month, whichever month
    // of the strip is being read above them.
    const auto currentRecord =
        std::find_if(records.cbegin(), records.cend(), [this, &current](const auto& record) {
            return record.month == current && record.flow.income.currency() == m_currency;
        });
    const bool hasCurrentFlow = currentRecord != records.cend();
    const domain::MonthFlow empty{};
    const domain::MonthFlow& flow = hasCurrentFlow ? currentRecord->flow : empty;
    m_incomeText = hasCurrentFlow ? formatMoneyPlain(flow.income) : QString{};
    m_needsText = hasCurrentFlow ? formatMoneyPlain(flow.needs) : QString{};
    m_wantsText = hasCurrentFlow ? formatMoneyPlain(flow.wants) : QString{};
    m_savingText = hasCurrentFlow ? formatMoneyPlain(flow.saving) : QString{};
    m_savedNote.clear();

    // The note under the form describes the month the form edits, not the
    // month the history strip happens to have selected above it.
    m_savingNote.clear();
    if (hasCurrentFlow) {
        const IncomeSplit currentSplit = incomeSplitOf(flow);
        const int rate = static_cast<int>(std::lround(currentSplit.saving.asFraction() * 100.0));
        if (currentSplit.saving < kSavingFloorReference)
            m_savingNote = tr("%1 % of income is going to saving. The model says pay yourself "
                              "first, 10–20 % including amortisation.")
                               .arg(rate);
        else
            m_savingNote =
                tr("%1 % of income is going to saving, inside the model's 10–20 % band.").arg(rate);
    }

    applySelection(months);
    emit changed();
}

void CashflowViewModel::applySelection(const QVector<FlowMonth>& months)
{
    AppController* controller = AppController::instance();
    const std::optional<domain::MonthFlow> selectedFlow =
        controller != nullptr ? controller->monthFlow(m_selectedMonth) : std::nullopt;

    QVector<HistoryColumn> columns;
    columns.reserve(months.size());
    int selectedRow = -1;
    FlowMonth selected;
    for (int index = 0; index < months.size(); ++index) {
        const FlowMonth& month = months.at(index);
        HistoryColumn column;
        column.label = monthLabel(month.month, QStringLiteral("MMM"));
        column.fullLabel = monthLabel(month.month, QStringLiteral("MMMM yyyy"));
        column.recorded = month.recorded;
        column.needsRatio = month.needsShare.asFraction();
        column.wantsRatio = month.wantsShare.asFraction();
        column.savingRatio = month.savingShare.asFraction();
        column.selected = month.month == m_selectedMonth;
        if (column.selected) {
            selectedRow = index;
            selected = month;
        }
        columns.append(column);
    }
    m_history->setRows(std::move(columns));
    if (selectedRow >= 0)
        m_history->setSelected(selectedRow);

    m_selectedMonthLabel = monthLabel(m_selectedMonth, QStringLiteral("MMMM yyyy"));
    m_selectedRecorded = selected.recorded;
    m_selectedSavingsRateLabel =
        selected.recorded ? percentLabel(selected.savingShare, 1) : QString{};

    // The shares are the domain's; the ratio the bar multiplies by pixels is
    // the same number as the percentage label beside it.
    m_split = IncomeSplit{};
    if (selected.recorded) {
        m_split.hasIncome = selectedFlow.has_value() && selectedFlow->income.isPositive() &&
                            selectedFlow->income.currency() == m_currency;
        m_split.needs = selected.needsShare;
        m_split.wants = selected.wantsShare;
        m_split.saving = selected.savingShare;
    }

    const bool amounts = selectedFlow.has_value() && selectedFlow->income.currency() == m_currency;
    m_incomeLabel = amounts ? formatMoney(selectedFlow->income) : tr("Not recorded");
    m_needsLabel = amounts ? formatMoney(selectedFlow->needs) : tr("Not recorded");
    m_wantsLabel = amounts ? formatMoney(selectedFlow->wants) : tr("Not recorded");
    m_savingLabel = amounts ? formatMoney(selectedFlow->saving) : tr("Not recorded");

    m_needsShareLabel = percentLabel(m_split.needs, 1);
    m_wantsShareLabel = percentLabel(m_split.wants, 1);
    m_savingShareLabel = percentLabel(m_split.saving, 1);
    m_needsTargetLabel = percentLabel(kNeedsTarget, 0);
    m_wantsTargetLabel = percentLabel(kWantsTarget, 0);
    m_savingTargetLabel = percentLabel(kSavingTarget, 0);
    m_needsDeviationLabel = deviationSentence(tr("Needs"), true, m_split.needs, kNeedsTarget);
    m_wantsDeviationLabel = deviationSentence(tr("Wants"), true, m_split.wants, kWantsTarget);
    m_savingDeviationLabel = deviationSentence(tr("Saving"), false, m_split.saving, kSavingTarget);

    // The savings rate over the same window, with both reference lines.
    const SavingsRateSeries series = savingsRateSeriesOf(months);
    m_savingsEmpty = series.empty;
    m_savingsPoints.clear();
    for (const ChartPoint& point : series.points) {
        m_savingsPoints.append(
            QVariantMap{{QStringLiteral("x"), point.x}, {QStringLiteral("y"), point.y}});
    }
    m_savingsAxisTopLabel = percentLabel(series.axisMax, 0);
    m_savingsReferences.clear();
    const double axis = series.axisMax.asFraction();
    if (axis > 0.0) {
        m_savingsReferences.append(
            QVariantMap{{QStringLiteral("y"), kSavingFloorReference.asFraction() / axis},
                        {QStringLiteral("label"), percentLabel(kSavingFloorReference, 0)}});
        m_savingsReferences.append(
            QVariantMap{{QStringLiteral("y"), kSavingGoalReference.asFraction() / axis},
                        {QStringLiteral("label"), percentLabel(kSavingGoalReference, 0)}});
    }
    m_savingsFirstLabel =
        months.isEmpty() ? QString{} : monthLabel(months.front().month, QStringLiteral("MMM"));
    m_savingsLastLabel =
        months.isEmpty() ? QString{} : monthLabel(months.back().month, QStringLiteral("MMM"));
}

void CashflowViewModel::selectMonth(int index)
{
    if (m_history->count() == 0)
        return;
    const int clamped = std::clamp(index, 0, m_history->count() - 1);

    // The history ends at the current month, so a row's distance from the end
    // is its distance from today.
    const domain::YearMonth current = AppController::currentMonth();
    const domain::YearMonth month = current.plusMonths(clamped - (m_history->count() - 1));
    if (month == m_selectedMonth)
        return;

    m_selectedMonth = month;
    reload();
}

QString CashflowViewModel::saveMonth(const QString& incomeText, const QString& needsText,
                                     const QString& wantsText, const QString& savingText)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_saveError = tr("The database is not open.");
        emit changed();
        return m_saveError;
    }

    if (incomeText.trimmed().isEmpty()) {
        m_saveError = tr("Enter this month's income.");
        emit changed();
        return m_saveError;
    }

    // Each field is read on its own so the message names what is wrong; an
    // empty field is a recorded zero, which is what a blank field looks like.
    const auto read = [this](const QString& text, domain::Money* amount) -> QString {
        if (text.trimmed().isEmpty()) {
            *amount = domain::Money::zero(m_currency);
            return {};
        }
        const MoneyInput parsed = parseMoneyInput(text, m_currency);
        if (!parsed.ok)
            return parsed.error;
        *amount = parsed.value;
        return {};
    };

    domain::MonthFlow flow;
    flow.income = domain::Money::zero(m_currency);
    flow.needs = domain::Money::zero(m_currency);
    flow.wants = domain::Money::zero(m_currency);
    flow.saving = domain::Money::zero(m_currency);

    for (const auto& entry : {std::pair<const QString*, domain::Money*>{&incomeText, &flow.income},
                              {&needsText, &flow.needs},
                              {&wantsText, &flow.wants},
                              {&savingText, &flow.saving}}) {
        const QString error = read(*entry.first, entry.second);
        if (!error.isEmpty()) {
            m_saveError = error;
            emit changed();
            return m_saveError;
        }
    }

    // Saving is part of income, not something on top of it: a month where the
    // three outflows exceed what came in cannot be split by the model at all.
    if (flow.income < flow.needs + flow.wants + flow.saving) {
        m_saveError = tr("Needs, wants and saving come to more than income.");
        emit changed();
        return m_saveError;
    }

    const application::Status status = controller->recordMonth(AppController::currentMonth(), flow);
    if (!status.ok()) {
        m_saveError = QString::fromStdString(status.error());
        emit changed();
        return m_saveError;
    }

    m_saveError.clear();
    reload();
    m_savedNote = tr("Recorded %1.").arg(m_currentMonthLabel);
    emit changed();
    return {};
}

} // namespace hinkwise::presentation
