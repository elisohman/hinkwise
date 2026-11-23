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

/// Reads an optional amount field: empty is zero, and anything else has to be
/// a readable amount in the profile's currency.
bool readOptionalMoney(const QString& text, domain::Currency currency, domain::Money* amount)
{
    if (text.trimmed().isEmpty()) {
        *amount = domain::Money::zero(currency);
        return true;
    }
    const MoneyInput parsed = parseMoneyInput(text, currency);
    if (!parsed.ok)
        return false;
    *amount = parsed.value;
    return true;
}

/// What the three breakdown fields say. `any` is false when all three are
/// empty, which is a month with no breakdown rather than a broken one. When
/// `any` is true, `problem` is empty only when all three are present and add
/// up to `saving`.
struct SplitReading {
    bool any = false;
    bool ok = true;
    std::optional<domain::SavingsBreakdown> split;
    QString problem;
};

SplitReading readSavingSplit(const QString& bufferText, const QString& investingText,
                             const QString& amortisationText, const domain::Money& saving)
{
    SplitReading reading;
    reading.any = !bufferText.trimmed().isEmpty() || !investingText.trimmed().isEmpty() ||
                  !amortisationText.trimmed().isEmpty();
    if (!reading.any)
        return reading;

    if (bufferText.trimmed().isEmpty() || investingText.trimmed().isEmpty() ||
        amortisationText.trimmed().isEmpty()) {
        reading.ok = false;
        reading.problem = CashflowViewModel::tr("Fill all three, or leave all three empty.");
        return reading;
    }

    const auto read = [&saving](const QString& text, domain::Money* amount) -> QString {
        const MoneyInput parsed = parseMoneyInput(text, saving.currency());
        if (!parsed.ok)
            return parsed.error;
        *amount = parsed.value;
        return {};
    };

    domain::SavingsBreakdown split;
    split.toBuffer = domain::Money::zero(saving.currency());
    split.toInvesting = domain::Money::zero(saving.currency());
    split.toAmortisation = domain::Money::zero(saving.currency());

    for (const auto& entry :
         {std::pair<const QString*, domain::Money*>{&bufferText, &split.toBuffer},
          {&investingText, &split.toInvesting},
          {&amortisationText, &split.toAmortisation}}) {
        const QString error = read(*entry.first, entry.second);
        if (!error.isEmpty()) {
            reading.ok = false;
            reading.problem = error;
            return reading;
        }
    }

    const domain::Money total = split.total();
    if (total == saving) {
        reading.split = split;
        return reading;
    }

    reading.ok = false;
    if (total < saving)
        reading.problem =
            CashflowViewModel::tr("%1 left to place.").arg(formatMoney(saving - total));
    else
        reading.problem = CashflowViewModel::tr("%1 too much.").arg(formatMoney(total - saving));
    return reading;
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
    reloadFromStore(dirty());
}

void CashflowViewModel::reloadFromStore(bool keepDraft)
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

    const QString loadedIncome = hasCurrentFlow ? formatMoneyPlain(flow.income) : QString{};
    const QString loadedNeeds = hasCurrentFlow ? formatMoneyPlain(flow.needs) : QString{};
    const QString loadedWants = hasCurrentFlow ? formatMoneyPlain(flow.wants) : QString{};
    const QString loadedSaving = hasCurrentFlow ? formatMoneyPlain(flow.saving) : QString{};
    const bool hasSplit = hasCurrentFlow && flow.savingSplit.has_value();
    const QString loadedBuffer =
        hasSplit ? formatMoneyPlain(flow.savingSplit->toBuffer) : QString{};
    const QString loadedInvesting =
        hasSplit ? formatMoneyPlain(flow.savingSplit->toInvesting) : QString{};
    const QString loadedAmortisation =
        hasSplit ? formatMoneyPlain(flow.savingSplit->toAmortisation) : QString{};

    // The baseline always follows the store, so Discard restores the newest
    // saved values. The draft only follows it when the household is not in the
    // middle of an edit: a reload must never throw typed text away.
    m_storedIncomeText = loadedIncome;
    m_storedNeedsText = loadedNeeds;
    m_storedWantsText = loadedWants;
    m_storedSavingText = loadedSaving;
    m_storedBufferSplitText = loadedBuffer;
    m_storedInvestingSplitText = loadedInvesting;
    m_storedAmortisationSplitText = loadedAmortisation;
    if (!keepDraft) {
        m_incomeText = loadedIncome;
        m_needsText = loadedNeeds;
        m_wantsText = loadedWants;
        m_savingText = loadedSaving;
        m_bufferSplitText = loadedBuffer;
        m_investingSplitText = loadedInvesting;
        m_amortisationSplitText = loadedAmortisation;
    }
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

    // The saving breakdown belongs to the selected month, which can be any of
    // the twelve columns, so it is read from that month's own flow rather than
    // from the current month's metrics.
    m_selectedHasSavingSplit = amounts && selectedFlow->savingSplit.has_value();
    m_bufferSavingShareLabel.clear();
    m_investingSavingShareLabel.clear();
    m_amortisationSavingShareLabel.clear();
    m_bufferSavingAmountLabel.clear();
    m_investingSavingAmountLabel.clear();
    m_amortisationSavingAmountLabel.clear();
    if (m_selectedHasSavingSplit) {
        const SavingsSplitShares shares = savingsSplitSharesOf(*selectedFlow);
        m_bufferSavingShareLabel = percentLabel(shares.buffer, 1);
        m_investingSavingShareLabel = percentLabel(shares.investing, 1);
        m_amortisationSavingShareLabel = percentLabel(shares.amortisation, 1);
        m_bufferSavingAmountLabel = formatMoney(selectedFlow->savingSplit->toBuffer);
        m_investingSavingAmountLabel = formatMoney(selectedFlow->savingSplit->toInvesting);
        m_amortisationSavingAmountLabel = formatMoney(selectedFlow->savingSplit->toAmortisation);
    }

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

QString CashflowViewModel::saveMonth()
{
    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_saveError = tr("The database is not open.");
        emit changed();
        return m_saveError;
    }

    if (m_incomeText.trimmed().isEmpty()) {
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

    for (const auto& entry :
         {std::pair<const QString*, domain::Money*>{&m_incomeText, &flow.income},
          {&m_needsText, &flow.needs},
          {&m_wantsText, &flow.wants},
          {&m_savingText, &flow.saving}}) {
        const QString error = read(*entry.first, entry.second);
        if (!error.isEmpty()) {
            m_saveError = error;
            emit changed();
            return m_saveError;
        }
    }

    // A breakdown is optional, but once one field is filled all three are and
    // they have to add up to the saving total. The repository also refuses a
    // split that does not, so this check is what lets the page explain why.
    const SplitReading split = readSavingSplit(m_bufferSplitText, m_investingSplitText,
                                               m_amortisationSplitText, flow.saving);
    if (!split.ok) {
        m_saveError = split.problem;
        emit changed();
        return m_saveError;
    }
    flow.savingSplit = split.split;

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
    // The draft is now what is stored, in the formatters' canonical spelling.
    reloadFromStore(false);
    m_savedNote = tr("Recorded %1.").arg(m_currentMonthLabel);
    emit changed();
    return {};
}

void CashflowViewModel::discardMonth()
{
    m_incomeText = m_storedIncomeText;
    m_needsText = m_storedNeedsText;
    m_wantsText = m_storedWantsText;
    m_savingText = m_storedSavingText;
    m_bufferSplitText = m_storedBufferSplitText;
    m_investingSplitText = m_storedInvestingSplitText;
    m_amortisationSplitText = m_storedAmortisationSplitText;
    m_saveError.clear();
    emit changed();
}

void CashflowViewModel::fillSavingSplitRemainder()
{
    if (!m_investingSplitText.trimmed().isEmpty())
        return;

    domain::Money saving{};
    domain::Money buffer{};
    domain::Money amortisation{};
    if (!readOptionalMoney(m_savingText, m_currency, &saving) ||
        !readOptionalMoney(m_bufferSplitText, m_currency, &buffer) ||
        !readOptionalMoney(m_amortisationSplitText, m_currency, &amortisation))
        return;

    const domain::Money remainder = saving - buffer - amortisation;
    if (remainder.isNegative())
        return;

    m_investingSplitText = formatMoneyPlain(remainder);
    emit changed();
}

bool CashflowViewModel::dirty() const
{
    return m_incomeText != m_storedIncomeText || m_needsText != m_storedNeedsText ||
           m_wantsText != m_storedWantsText || m_savingText != m_storedSavingText ||
           m_bufferSplitText != m_storedBufferSplitText ||
           m_investingSplitText != m_storedInvestingSplitText ||
           m_amortisationSplitText != m_storedAmortisationSplitText;
}

bool CashflowViewModel::anySavingSplitText() const
{
    return !m_bufferSplitText.trimmed().isEmpty() || !m_investingSplitText.trimmed().isEmpty() ||
           !m_amortisationSplitText.trimmed().isEmpty();
}

QString CashflowViewModel::savingSplitError() const
{
    if (!anySavingSplitText())
        return {};

    domain::Money saving{};
    if (!readOptionalMoney(m_savingText, m_currency, &saving))
        return tr("Enter a saving amount that can be read before splitting it.");

    return readSavingSplit(m_bufferSplitText, m_investingSplitText, m_amortisationSplitText, saving)
        .problem;
}

bool CashflowViewModel::savingSplitValid() const
{
    return savingSplitError().isEmpty();
}

bool CashflowViewModel::savingSplitCanFill() const
{
    // Filling the remainder is only offered once a part is known and while
    // investments is the field nobody has filled in yet; otherwise the action
    // would overwrite an amount the household already entered.
    if (!m_investingSplitText.trimmed().isEmpty())
        return false;
    if (m_bufferSplitText.trimmed().isEmpty() && m_amortisationSplitText.trimmed().isEmpty())
        return false;

    domain::Money saving{};
    domain::Money buffer{};
    domain::Money amortisation{};
    if (!readOptionalMoney(m_savingText, m_currency, &saving) ||
        !readOptionalMoney(m_bufferSplitText, m_currency, &buffer) ||
        !readOptionalMoney(m_amortisationSplitText, m_currency, &amortisation))
        return false;

    const domain::Money remainder = saving - buffer - amortisation;
    return !remainder.isNegative();
}

void CashflowViewModel::setIncomeText(const QString& text)
{
    if (text == m_incomeText)
        return;
    m_incomeText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setNeedsText(const QString& text)
{
    if (text == m_needsText)
        return;
    m_needsText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setWantsText(const QString& text)
{
    if (text == m_wantsText)
        return;
    m_wantsText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setSavingText(const QString& text)
{
    if (text == m_savingText)
        return;
    m_savingText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setBufferSplitText(const QString& text)
{
    if (text == m_bufferSplitText)
        return;
    m_bufferSplitText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setInvestingSplitText(const QString& text)
{
    if (text == m_investingSplitText)
        return;
    m_investingSplitText = text;
    m_savedNote.clear();
    emit changed();
}

void CashflowViewModel::setAmortisationSplitText(const QString& text)
{
    if (text == m_amortisationSplitText)
        return;
    m_amortisationSplitText = text;
    m_savedNote.clear();
    emit changed();
}

} // namespace hinkwise::presentation
