// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wizard_view_model.h"

#include "app_controller.h"
#include "catalog.h"
#include "domain/phase.h"
#include "input_text.h"

#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

constexpr int kStepCount = 5;
constexpr int kAccountsStep = 3;
constexpr int kReviewStep = 4;

constexpr int kAssetKindCount = 10;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

domain::Phase phaseAt(int index)
{
    return domain::allPhases()[static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(domain::allPhases().size()) - 1))];
}

domain::AssetKind assetKindAt(int index)
{
    return static_cast<domain::AssetKind>(std::clamp(index, 0, kAssetKindCount - 1));
}

} // namespace

// ---------------------------------------------------------------------------
// Account drafts
// ---------------------------------------------------------------------------

AccountDraftModel::AccountDraftModel(QObject* parent)
    : QAbstractListModel(parent)
{
    reset();
}

void AccountDraftModel::reset()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();

    // The two rows a household most often starts from. They are a starting
    // point, not a statement: the names are editable and both can be removed.
    addRow();
    setName(0, tr("Savings account"));
    setKind(0, static_cast<int>(domain::AssetKind::BankAccount));
    addRow();
    setName(1, tr("Index fund"));
    setKind(1, static_cast<int>(domain::AssetKind::Fund));
}

int AccountDraftModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant AccountDraftModel::data(const QModelIndex& index, int role) const
{
    const int row = indexOf(index);
    if (row < 0)
        return QVariant{};

    const Row& draft = m_rows.at(row);
    switch (role) {
    case NameRole:
        return QVariant{draft.name};
    case KindIndexRole:
        return QVariant{static_cast<int>(draft.kind)};
    case KindLabelRole:
        return QVariant{catalog::assetKindLabel(draft.kind)};
    case BucketLabelRole:
        return QVariant{catalog::bucketLabel(domain::factsOf(draft.kind).defaultBucket)};
    case BalanceTextRole:
        return QVariant{draft.balanceText};
    case BalanceLabelRole:
        return QVariant{draft.balanceLabel};
    case BalanceErrorRole:
        return QVariant{draft.balanceError};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> AccountDraftModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {KindIndexRole, "kindIndex"},
        {KindLabelRole, "kindLabel"},
        {BucketLabelRole, "bucketLabel"},
        {BalanceTextRole, "balanceText"},
        {BalanceLabelRole, "balanceLabel"},
        {BalanceErrorRole, "balanceError"},
    };
}

int AccountDraftModel::indexOf(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return -1;
    return index.row();
}

void AccountDraftModel::refreshRow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed,
                     {NameRole, KindIndexRole, KindLabelRole, BucketLabelRole, BalanceTextRole,
                      BalanceLabelRole, BalanceErrorRole});
}

void AccountDraftModel::setName(int row, const QString& name)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).name == name)
        return;
    m_rows[row].name = name;
    refreshRow(row);
}

void AccountDraftModel::setKind(int row, int kindIndex)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const domain::AssetKind kind = assetKindAt(kindIndex);
    if (m_rows.at(row).kind == kind)
        return;
    m_rows[row].kind = kind;
    refreshRow(row);
}

void AccountDraftModel::setBalanceText(int row, const QString& text)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).balanceText == text)
        return;
    m_rows[row].balanceText = text;
    applyBalance(m_rows[row]);
    refreshRow(row);
}

void AccountDraftModel::addRow()
{
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex{}, row, row);
    Row draft;
    draft.id = newId();
    draft.balance = domain::Money::zero(m_currency);
    m_rows.push_back(std::move(draft));
    endInsertRows();
    emit countChanged();
}

void AccountDraftModel::removeRow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex{}, row, row);
    m_rows.remove(row);
    endRemoveRows();
    emit countChanged();
}

void AccountDraftModel::setCurrency(domain::Currency currency)
{
    if (m_currency == currency)
        return;
    m_currency = currency;
    for (Row& draft : m_rows)
        applyBalance(draft);
    if (m_rows.isEmpty())
        return;
    emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, 0),
                     {BalanceLabelRole, BalanceErrorRole});
}

bool AccountDraftModel::complete() const
{
    if (m_rows.isEmpty())
        return false;
    return std::all_of(m_rows.cbegin(), m_rows.cend(), [](const Row& draft) {
        return !draft.name.trimmed().isEmpty() && draft.balanceOk;
    });
}

QString AccountDraftModel::firstProblem() const
{
    if (m_rows.isEmpty())
        return tr("Add at least one account.");
    for (const Row& draft : m_rows) {
        if (draft.name.trimmed().isEmpty())
            return tr("Give every account a name.");
        if (!draft.balanceOk) {
            if (!draft.balanceError.isEmpty())
                return draft.balanceError;
            return tr("Enter a balance for \"%1\".").arg(draft.name.trimmed());
        }
    }
    return {};
}

std::vector<domain::Asset> AccountDraftModel::toAssets() const
{
    std::vector<domain::Asset> assets;
    assets.reserve(static_cast<std::size_t>(m_rows.size()));
    for (const Row& draft : m_rows)
        assets.push_back(domain::makeAsset(draft.id.toStdString(),
                                           draft.name.trimmed().toStdString(), draft.kind));
    return assets;
}

QVector<domain::Money> AccountDraftModel::balances() const
{
    QVector<domain::Money> amounts;
    amounts.reserve(m_rows.size());
    for (const Row& draft : m_rows)
        amounts.append(draft.balance);
    return amounts;
}

void AccountDraftModel::applyBalance(Row& draft)
{
    if (draft.balanceText.trimmed().isEmpty()) {
        draft.balanceOk = false;
        draft.balanceError.clear();
        draft.balanceLabel.clear();
        draft.balance = domain::Money::zero(m_currency);
        return;
    }
    const MoneyInput parsed = parseMoneyInput(draft.balanceText, m_currency);
    draft.balanceOk = parsed.ok;
    draft.balanceError = parsed.error;
    draft.balanceLabel = parsed.label;
    draft.balance = parsed.value;
}

// ---------------------------------------------------------------------------
// Wizard
// ---------------------------------------------------------------------------

WizardViewModel::WizardViewModel(QObject* parent)
    : QObject(parent)
    , m_accounts(new AccountDraftModel(this))
{
    // canGoNext and blockingReason are NOTIFY changed, so every way an account
    // row can change has to announce it. Without these the Next button keeps
    // the state it had before the household filled in the last balance.
    connect(m_accounts, &AccountDraftModel::dataChanged, this, &WizardViewModel::changed);
    connect(m_accounts, &AccountDraftModel::rowsInserted, this, &WizardViewModel::changed);
    connect(m_accounts, &AccountDraftModel::rowsRemoved, this, &WizardViewModel::changed);

    m_income.value = domain::Money::zero(currency());
    m_needs.value = domain::Money::zero(currency());
    m_wants.value = domain::Money::zero(currency());
}

void WizardViewModel::reset()
{
    m_step = 0;
    m_currencyIndex = 0;
    m_phaseIndex = 0;
    m_trackingModeIndex = static_cast<int>(domain::TrackingMode::Planning);
    m_createError.clear();

    m_income = Field{};
    m_needs = Field{};
    m_wants = Field{};
    m_income.value = domain::Money::zero(currency());
    m_needs.value = domain::Money::zero(currency());
    m_wants.value = domain::Money::zero(currency());

    m_accounts->reset();
    emit changed();
}

int WizardViewModel::stepCount() const
{
    return kStepCount;
}

domain::Currency WizardViewModel::currency() const
{
    static constexpr domain::Currency kCurrencies[] = {domain::Currency::Sek, domain::Currency::Eur,
                                                       domain::Currency::Usd};
    return kCurrencies[std::clamp(m_currencyIndex, 0, 2)];
}

QString WizardViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(currency()));
}

QVariantList WizardViewModel::currencyChoices() const
{
    return {
        QVariantMap{{QStringLiteral("index"), 0},
                    {QStringLiteral("code"), QStringLiteral("SEK")},
                    {QStringLiteral("name"), tr("Swedish krona")}},
        QVariantMap{{QStringLiteral("index"), 1},
                    {QStringLiteral("code"), QStringLiteral("EUR")},
                    {QStringLiteral("name"), tr("Euro")}},
        QVariantMap{{QStringLiteral("index"), 2},
                    {QStringLiteral("code"), QStringLiteral("USD")},
                    {QStringLiteral("name"), tr("US dollar")}},
    };
}

QVariantList WizardViewModel::phaseChoices() const
{
    QVariantList choices;
    const std::array<domain::Phase, 4> phases = domain::allPhases();
    for (std::size_t position = 0; position < phases.size(); ++position) {
        const domain::Phase phase = phases[position];
        QString description;
        switch (phase) {
        case domain::Phase::GettingStarted:
            description = tr("Little saved, and the first buffer matters more than the return.");
            break;
        case domain::Phase::BuildingWealth:
            description = tr("Income above spending every month, and the buckets are filling.");
            break;
        case domain::Phase::Preserving:
            description = tr("The capital is large enough to protect while it pays out.");
            break;
        case domain::Phase::FinanciallyIndependent:
            description = tr("The capital covers the life, and the plan is to spend it well.");
            break;
        }
        choices.append(
            QVariantMap{{QStringLiteral("index"), static_cast<int>(position)},
                        {QStringLiteral("label"), QString::fromUtf8(domain::labelOf(phase))},
                        {QStringLiteral("description"), description}});
    }
    return choices;
}

QString WizardViewModel::phaseLabel() const
{
    return QString::fromUtf8(domain::labelOf(phaseAt(m_phaseIndex)));
}

QVariantList WizardViewModel::trackingModeChoices() const
{
    const auto choice = [](domain::TrackingMode mode) {
        return QVariantMap{
            {QStringLiteral("index"), static_cast<int>(mode)},
            {QStringLiteral("label"), catalog::trackingModeLabel(mode)},
            {QStringLiteral("description"), catalog::trackingModeDescription(mode)},
        };
    };
    // Accounting first, the same order the settings page offers.
    return {choice(domain::TrackingMode::Accounting), choice(domain::TrackingMode::Planning)};
}

domain::TrackingMode WizardViewModel::trackingMode() const
{
    return m_trackingModeIndex == static_cast<int>(domain::TrackingMode::Accounting)
               ? domain::TrackingMode::Accounting
               : domain::TrackingMode::Planning;
}

QString WizardViewModel::trackingModeLabel() const
{
    return catalog::trackingModeLabel(trackingMode());
}

bool WizardViewModel::incomeValid() const
{
    if (!m_income.ok || !m_needs.ok || !m_wants.ok)
        return false;
    return !(m_income.value - m_needs.value - m_wants.value).isNegative();
}

QString WizardViewModel::savingLabel() const
{
    if (!incomeValid())
        return {};
    return formatMoney(m_income.value - m_needs.value - m_wants.value);
}

QString WizardViewModel::savingNote() const
{
    if (!incomeValid())
        return tr("Saving is whatever is left after needs and wants.");
    if (!m_income.value.isPositive())
        return tr("With no income there is nothing to save yet.");
    const std::int64_t saving = (m_income.value - m_needs.value - m_wants.value).minorUnits();
    const double share =
        100.0 * static_cast<double>(saving) / static_cast<double>(m_income.value.minorUnits());
    return tr("%1 % of your income. The model suggests saving 10–20 %, and sizes "
              "your buckets from what you spend.")
        .arg(std::lround(share));
}

bool WizardViewModel::stepValid(int step) const
{
    switch (step) {
    case 2:
        return incomeValid();
    case kAccountsStep:
        return m_accounts != nullptr && m_accounts->complete();
    case kReviewStep:
        return incomeValid() && m_accounts != nullptr && m_accounts->complete();
    default:
        return true;
    }
}

bool WizardViewModel::canGoNext() const
{
    return m_step < kReviewStep && stepValid(m_step);
}

bool WizardViewModel::isReview() const
{
    return m_step == kReviewStep;
}

bool WizardViewModel::canCreate() const
{
    return isReview() && stepValid(kReviewStep);
}

QStringList WizardViewModel::assetKindLabels() const
{
    return catalog::assetKindLabels();
}

QString WizardViewModel::blockingReason() const
{
    if (stepValid(m_step))
        return {};

    if (m_step == kAccountsStep)
        return m_accounts->firstProblem();

    if (!m_income.error.isEmpty())
        return m_income.error;
    if (!m_needs.error.isEmpty())
        return m_needs.error;
    if (!m_wants.error.isEmpty())
        return m_wants.error;
    if (m_income.text.trimmed().isEmpty())
        return tr("Enter your monthly income after tax.");
    if (m_needs.text.trimmed().isEmpty())
        return tr("Enter what you spend on needs each month.");
    if (m_wants.text.trimmed().isEmpty())
        return tr("Enter what you spend on wants each month.");
    if (incomeValid())
        return {};
    return tr("Needs and wants come to more than your income.");
}

QVariantList WizardViewModel::reviewLines() const
{
    QVariantList lines;
    const auto add = [&lines](const QString& label, const QString& value) {
        lines.append(
            QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
    };

    add(tr("Currency"), currencyCode());
    add(tr("Phase"), phaseLabel());
    add(tr("How you use hinkwise"), trackingModeLabel());
    add(tr("Income after tax"), m_income.ok ? formatMoney(m_income.value) : m_income.text);
    add(tr("Needs"), m_needs.ok ? formatMoney(m_needs.value) : m_needs.text);
    add(tr("Wants"), m_wants.ok ? formatMoney(m_wants.value) : m_wants.text);
    add(tr("Saving"), savingLabel());
    return lines;
}

void WizardViewModel::setCurrencyIndex(int index)
{
    const int clamped = std::clamp(index, 0, 2);
    if (clamped == m_currencyIndex)
        return;
    m_currencyIndex = clamped;
    applyCurrency();
    emit changed();
}

void WizardViewModel::setPhaseIndex(int index)
{
    const int clamped = std::clamp(index, 0, static_cast<int>(domain::allPhases().size()) - 1);
    if (clamped == m_phaseIndex)
        return;
    m_phaseIndex = clamped;
    emit changed();
}

void WizardViewModel::setTrackingModeIndex(int index)
{
    const int clamped = std::clamp(index, 0, 1);
    if (clamped == m_trackingModeIndex)
        return;
    m_trackingModeIndex = clamped;
    emit changed();
}

void WizardViewModel::setIncomeText(const QString& text)
{
    if (text == m_income.text)
        return;
    applyField(m_income, text);
    emit changed();
}

void WizardViewModel::setNeedsText(const QString& text)
{
    if (text == m_needs.text)
        return;
    applyField(m_needs, text);
    emit changed();
}

void WizardViewModel::setWantsText(const QString& text)
{
    if (text == m_wants.text)
        return;
    applyField(m_wants, text);
    emit changed();
}

void WizardViewModel::applyField(Field& field, const QString& text)
{
    field.text = text;
    if (text.trimmed().isEmpty()) {
        // An untouched field is not an error yet: the reason Next is disabled
        // is shown once, under the step, rather than three times in red.
        field.ok = false;
        field.error.clear();
        field.value = domain::Money::zero(currency());
        return;
    }
    const MoneyInput parsed = parseMoneyInput(text, currency());
    field.ok = parsed.ok;
    field.error = parsed.error;
    field.value = parsed.value;
}

void WizardViewModel::applyCurrency()
{
    const domain::Currency selected = currency();
    m_accounts->setCurrency(selected);
    applyField(m_income, m_income.text);
    applyField(m_needs, m_needs.text);
    applyField(m_wants, m_wants.text);
}

void WizardViewModel::next()
{
    if (!canGoNext())
        return;
    ++m_step;
    emit changed();
}

void WizardViewModel::back()
{
    if (!canGoBack())
        return;
    --m_step;
    emit changed();
}

bool WizardViewModel::createEconomy()
{
    const auto fail = [this](const QString& message) {
        m_createError = message;
        emit changed();
        return false;
    };

    if (!stepValid(kReviewStep))
        return fail(blockingReason());

    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return fail(tr("The database is not open."));

    const domain::YearMonth month = AppController::currentMonth();
    const std::vector<domain::Asset> assets = m_accounts->toAssets();
    const QVector<domain::Money> balances = m_accounts->balances();
    const domain::MonthFlow flow{m_income.value, m_needs.value, m_wants.value,
                                 m_income.value - m_needs.value - m_wants.value};

    // A household is a profile, its accounts, the month's flow and the balances
    // of those accounts: one decision, so one transaction. Without it a failure
    // halfway leaves a profile the onboarding screen reads as "set up" and no
    // accounts to go with it. Committed only once the last balance is stored;
    // dropped uncommitted, which rolls the lot back, on the first failure.
    application::Result<std::unique_ptr<application::Transaction>> begun =
        controller->beginTransaction();
    if (!begun.ok())
        return fail(QString::fromStdString(begun.error()));
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    application::Status status =
        controller->createHousehold(currency(), phaseAt(m_phaseIndex), trackingMode());

    for (const domain::Asset& asset : assets) {
        if (!status.ok())
            break;
        status = controller->addAsset(asset);
    }

    if (status.ok())
        status = controller->recordMonth(month, flow);

    for (std::size_t index = 0; index < assets.size(); ++index) {
        if (!status.ok())
            break;
        status = controller->recordValuation(assets[index].id, month,
                                             balances.at(static_cast<int>(index)));
    }

    if (status.ok())
        status = transaction->commit();

    if (!status.ok()) {
        transaction.reset();
        return fail(QString::fromStdString(status.error()));
    }

    m_createError.clear();
    emit changed();
    return true;
}

} // namespace hinkwise::presentation
