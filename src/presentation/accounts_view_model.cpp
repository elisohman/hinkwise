// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "accounts_view_model.h"

#include "app_controller.h"
#include "catalog.h"
#include "input_text.h"

#include <QDate>
#include <QLocale>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hinkwise::presentation {

namespace {

constexpr int kAssetKindCount = 10;
constexpr int kLiabilityKindCount = 6;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

domain::AssetKind assetKindAt(int index)
{
    return static_cast<domain::AssetKind>(std::clamp(index, 0, kAssetKindCount - 1));
}

domain::LiabilityKind liabilityKindAt(int index)
{
    return static_cast<domain::LiabilityKind>(std::clamp(index, 0, kLiabilityKindCount - 1));
}

QString message(const application::Status& status)
{
    return QString::fromStdString(status.error());
}

/// The repayment terms a debt dialog collected, read before anything is
/// written. A style the household did not pick contributes nothing: a fixed
/// loan has no fee and no term, so a value left in a hidden field cannot leak
/// into the row that is saved.
struct ParsedDebtTerms {
    bool ok = false;
    domain::RepaymentStyle style = domain::RepaymentStyle::FixedAmortisation;
    domain::Money payment{};
    /// MONTHLY, the unit Liability stores: the annual amount read here is
    /// spread over twelve before it reaches the model.
    domain::Money monthlyFee{};
    int termMonths = 0;
    QString error;
};

ParsedDebtTerms readDebtTerms(int styleIndex, const QString& paymentText,
                              const QString& annualFeeText, const QString& termYearsText,
                              domain::Currency currency)
{
    ParsedDebtTerms terms;
    terms.style = styleIndex == static_cast<int>(domain::RepaymentStyle::Annuity)
                      ? domain::RepaymentStyle::Annuity
                      : domain::RepaymentStyle::FixedAmortisation;
    terms.payment = domain::Money::zero(currency);
    terms.monthlyFee = domain::Money::zero(currency);

    // An unstated amount is zero, which is honest: the model reads a loan's
    // payment to know whether it can fall, and zero means no terms were
    // recorded rather than a payment of nothing that still clears it.
    if (!paymentText.trimmed().isEmpty()) {
        const MoneyInput parsed = parseMoneyInput(paymentText, currency);
        if (!parsed.ok) {
            terms.error = parsed.error;
            return terms;
        }
        terms.payment = parsed.value;
    }

    if (terms.style == domain::RepaymentStyle::FixedAmortisation) {
        terms.ok = true;
        return terms;
    }

    if (!annualFeeText.trimmed().isEmpty()) {
        const MoneyInput parsed = parseMoneyInput(annualFeeText, currency);
        if (!parsed.ok) {
            terms.error = parsed.error;
            return terms;
        }
        terms.monthlyFee = domain::monthlyFeeFromAnnual(parsed.value);
    }

    if (!termYearsText.trimmed().isEmpty()) {
        const TermYearsInput parsed = parseTermYearsInput(termYearsText);
        if (!parsed.ok) {
            terms.error = parsed.error;
            return terms;
        }
        terms.termMonths = parsed.months;
    }

    terms.ok = true;
    return terms;
}

} // namespace

// ---------------------------------------------------------------------------
// Account rows
// ---------------------------------------------------------------------------

AccountRowModel::AccountRowModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AccountRowModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant AccountRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const AccountRow& row = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return QVariant{row.id};
    case NameRole:
        return QVariant{row.name};
    case KindIndexRole:
        return QVariant{row.kindIndex};
    case KindLabelRole:
        return QVariant{row.kindLabel};
    case BucketIndexRole:
        return QVariant{row.bucketIndex};
    case BucketLabelRole:
        return QVariant{row.bucketLabel};
    case BalanceLabelRole:
        return QVariant{row.balanceLabel};
    case MonthLabelRole:
        return QVariant{row.monthLabel};
    case HasMonthValueRole:
        return QVariant{row.hasMonthValue};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> AccountRowModel::roleNames() const
{
    return {
        {IdRole, "accountId"},
        {NameRole, "name"},
        {KindIndexRole, "kindIndex"},
        {KindLabelRole, "kindLabel"},
        {BucketIndexRole, "bucketIndex"},
        {BucketLabelRole, "bucketLabel"},
        {BalanceLabelRole, "balanceLabel"},
        {MonthLabelRole, "monthLabel"},
        {HasMonthValueRole, "hasMonthValue"},
    };
}

void AccountRowModel::setRows(QVector<AccountRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------
// Debt rows
// ---------------------------------------------------------------------------

DebtRowModel::DebtRowModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int DebtRowModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant DebtRowModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant{};

    const DebtRow& row = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return QVariant{row.id};
    case NameRole:
        return QVariant{row.name};
    case KindIndexRole:
        return QVariant{row.kindIndex};
    case KindLabelRole:
        return QVariant{row.kindLabel};
    case RateLabelRole:
        return QVariant{row.rateLabel};
    case StyleIndexRole:
        return QVariant{row.styleIndex};
    case RateTextRole:
        return QVariant{row.rateText};
    case PaymentTextRole:
        return QVariant{row.paymentText};
    case FeeTextRole:
        return QVariant{row.feeText};
    case TermYearsTextRole:
        return QVariant{row.termYearsText};
    case BalanceLabelRole:
        return QVariant{row.balanceLabel};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> DebtRowModel::roleNames() const
{
    return {
        {IdRole, "debtId"},
        {NameRole, "name"},
        {KindIndexRole, "kindIndex"},
        {KindLabelRole, "kindLabel"},
        {RateLabelRole, "rateLabel"},
        {StyleIndexRole, "styleIndex"},
        {RateTextRole, "rateText"},
        {PaymentTextRole, "paymentText"},
        {FeeTextRole, "feeText"},
        {TermYearsTextRole, "termYearsText"},
        {BalanceLabelRole, "balanceLabel"},
    };
}

void DebtRowModel::setRows(QVector<DebtRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------
// Buckets page
// ---------------------------------------------------------------------------

AccountsViewModel::AccountsViewModel(QObject* parent)
    : QObject(parent)
    , m_buckets(new BucketsModel(this))
    , m_accounts(new AccountRowModel(this))
    , m_debts(new DebtRowModel(this))
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &AccountsViewModel::reload);
    }
    reload();
}

void AccountsViewModel::reload()
{
    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_buckets->setRows({});
        m_allAccounts.clear();
        m_accounts->setRows({});
        m_debts->setRows({});
        m_bucketRows.clear();
        emit changed();
        return;
    }

    const application::DashboardSnapshot snapshot = controller->dashboardSnapshot();
    m_currency = snapshot.profile.baseCurrency;

    m_bucketRows = bucketRowsOf(snapshot);
    m_buckets->setRows(m_bucketRows);

    const QLocale locale;
    m_monthLabel = locale.toString(QDate::currentDate(), QStringLiteral("MMMM yyyy"));

    // The page shows each account twice: what it is worth now, which is the
    // latest valuation whatever month it belongs to, and what was recorded for
    // the month named at the top, which may not exist yet.
    const domain::YearMonth month = AppController::currentMonth();
    QVector<AccountRow> accounts;
    for (const domain::Asset& asset : controller->assets()) {
        AccountRow row;
        row.id = QString::fromStdString(asset.id);
        row.name = QString::fromStdString(asset.name);
        row.kindIndex = static_cast<int>(asset.kind);
        row.kindLabel = catalog::assetKindLabel(asset.kind);
        row.bucketIndex = static_cast<int>(asset.bucket);
        row.bucketLabel = QString::fromUtf8(domain::labelOf(asset.bucket));

        const std::optional<domain::Money> latest = controller->latestAssetBalance(asset.id);
        row.balanceLabel = latest.has_value() && latest->currency() == m_currency
                               ? formatMoney(*latest)
                               : tr("Not valued");

        const std::optional<domain::Money> thisMonth = controller->assetBalanceAt(asset.id, month);
        row.hasMonthValue = thisMonth.has_value() && thisMonth->currency() == m_currency;
        row.monthLabel = row.hasMonthValue ? formatMoney(*thisMonth) : tr("Not recorded");
        accounts.append(row);
    }
    m_allAccounts = std::move(accounts);
    applyFilter();

    QVector<DebtRow> debts;
    for (const domain::Liability& liability : controller->liabilities()) {
        DebtRow row;
        row.id = QString::fromStdString(liability.id);
        row.name = QString::fromStdString(liability.name);
        row.kindIndex = static_cast<int>(liability.kind);
        row.kindLabel = catalog::liabilityKindLabel(liability.kind);
        row.rateLabel = formatInterestRate(liability.interestRate);
        row.styleIndex = static_cast<int>(liability.style);
        row.rateText = formatInterestRatePlain(liability.interestRate);
        // The payment and fee are amounts in the household's one base currency,
        // the same reading the plan makes of them, so a field reopens with the
        // figure the model has been using. The field collects a year of fee,
        // while the model spreads it over the months, so it shows twelve of the
        // stored monthly charge.
        const domain::Money payment{liability.regularPayment.minorUnits(), m_currency};
        const domain::Money monthlyFee{liability.monthlyFee.minorUnits(), m_currency};
        const domain::Money annualFee = domain::annualFeeFromMonthly(monthlyFee);
        row.paymentText = payment.isZero() ? QString{} : formatMoneyPlain(payment);
        row.feeText = annualFee.isZero() ? QString{} : formatMoneyPlain(annualFee);
        row.termYearsText = formatTermYears(liability.termMonths);

        const std::optional<domain::Money> balance = controller->latestDebtBalance(liability.id);
        row.balanceLabel = balance.has_value() && balance->currency() == m_currency
                               ? formatMoney(*balance)
                               : tr("Not valued");
        debts.append(row);
    }
    m_debts->setRows(std::move(debts));

    emit changed();
}

void AccountsViewModel::applyFilter()
{
    QVector<AccountRow> rows;
    for (const AccountRow& row : m_allAccounts) {
        if (row.bucketIndex == m_selectedBucket)
            rows.append(row);
    }
    m_accounts->setRows(std::move(rows));
}

void AccountsViewModel::setSelectedBucket(int index)
{
    const int clamped = std::clamp(index, 0, static_cast<int>(domain::allBuckets().size()) - 1);
    if (clamped == m_selectedBucket)
        return;
    m_selectedBucket = clamped;
    applyFilter();
    emit changed();
}

int AccountsViewModel::selectedBucket() const
{
    return m_selectedBucket;
}

QString AccountsViewModel::selectedBucketLabel() const
{
    return m_bucketRows.isEmpty() ? QString{} : m_bucketRows.at(m_selectedBucket).label;
}

QString AccountsViewModel::selectedBucketColourKey() const
{
    return m_bucketRows.isEmpty() ? QString{} : m_bucketRows.at(m_selectedBucket).colourKey;
}

QString AccountsViewModel::bucketBalanceLabel() const
{
    return m_bucketRows.isEmpty() ? QString{} : m_bucketRows.at(m_selectedBucket).balanceLabel;
}

QString AccountsViewModel::bucketTargetLabel() const
{
    return m_bucketRows.isEmpty() ? QString{} : m_bucketRows.at(m_selectedBucket).targetLabel;
}

QString AccountsViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_currency));
}

QStringList AccountsViewModel::assetKindLabels() const
{
    return catalog::assetKindLabels();
}

QStringList AccountsViewModel::liabilityKindLabels() const
{
    return catalog::liabilityKindLabels();
}

QStringList AccountsViewModel::bucketLabels() const
{
    return catalog::bucketLabels();
}

domain::Bucket AccountsViewModel::bucketAt(int index) const
{
    const int clamped = std::clamp(index, 0, static_cast<int>(domain::allBuckets().size()) - 1);
    return domain::allBuckets()[static_cast<std::size_t>(clamped)];
}

int AccountsViewModel::defaultBucketForKind(int kindIndex) const
{
    return static_cast<int>(domain::factsOf(assetKindAt(kindIndex)).defaultBucket);
}

int AccountsViewModel::firstKindForBucket(int bucketIndex) const
{
    const domain::Bucket bucket = bucketAt(bucketIndex);
    for (int index = 0; index <= static_cast<int>(domain::AssetKind::Other); ++index) {
        if (domain::factsOf(static_cast<domain::AssetKind>(index)).defaultBucket == bucket)
            return index;
    }
    return 0;
}

QString AccountsViewModel::latestBalanceText(const QString& assetId) const
{
    const AppController* controller = AppController::instance();
    if (controller == nullptr)
        return {};
    const std::optional<domain::Money> latest =
        controller->latestAssetBalance(assetId.toStdString());
    return latest.has_value() ? formatMoneyPlain(*latest) : QString{};
}

QString AccountsViewModel::addAccount(const QString& name, int kindIndex, int bucketIndex)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Give the account a name.");

    // The bucket override is allowed, but it starts from the model's answer for
    // the kind, which is what makeAsset() sets before we replace it.
    const domain::Asset asset = domain::withBucket(
        domain::makeAsset(newId().toStdString(), trimmed.toStdString(), assetKindAt(kindIndex)),
        bucketAt(bucketIndex));

    const application::Status status = controller->addAsset(asset);
    return status.ok() ? QString{} : message(status);
}

QString AccountsViewModel::updateAccount(const QString& assetId, const QString& name, int kindIndex,
                                         int bucketIndex)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Give the account a name.");

    const std::string id = assetId.toStdString();
    const std::vector<domain::Asset> existing = controller->assets();
    const auto found = std::find_if(existing.cbegin(), existing.cend(),
                                    [&id](const domain::Asset& asset) { return asset.id == id; });
    if (found == existing.cend())
        return tr("That account is no longer there. Reload the page.");

    // Rebuilt rather than patched, so a kind change also refreshes the
    // liquidity the model derives from the kind. Only the household's own
    // choice about the headline figure is carried over.
    domain::Asset updated =
        domain::makeAsset(found->id, trimmed.toStdString(), assetKindAt(kindIndex));
    updated.bucket = bucketAt(bucketIndex);
    updated.includedInHeadline = found->includedInHeadline;

    const application::Status status = controller->addAsset(updated);
    return status.ok() ? QString{} : message(status);
}

QString AccountsViewModel::removeAccount(const QString& assetId)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const application::Status status = controller->removeAsset(assetId.toStdString());
    return status.ok() ? QString{} : message(status);
}

QString AccountsViewModel::recordValuation(const QString& assetId, const QString& balanceText)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const MoneyInput amount = parseMoneyInput(balanceText, m_currency);
    if (!amount.ok)
        return amount.error;

    const application::Status status = controller->recordValuation(
        assetId.toStdString(), AppController::currentMonth(), amount.value);
    return status.ok() ? QString{} : message(status);
}

QString AccountsViewModel::recordMonthBalances(const QVariantMap& balances)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    // Everything is read before anything is written: a typo in the third field
    // must not leave the first two recorded and the month half done.
    struct Pending {
        std::string id;
        domain::Money balance;
    };
    std::vector<Pending> pending;
    pending.reserve(static_cast<std::size_t>(balances.size()));
    for (auto entry = balances.constBegin(); entry != balances.constEnd(); ++entry) {
        const QString text = entry.value().toString();
        if (text.trimmed().isEmpty())
            continue;
        const MoneyInput amount = parseMoneyInput(text, m_currency);
        if (!amount.ok)
            return tr("%1: %2").arg(entry.key(), amount.error);
        pending.push_back(Pending{entry.key().toStdString(), amount.value});
    }

    const domain::YearMonth month = AppController::currentMonth();

    // The month is recorded whole or not at all: every account's figure goes in
    // inside one transaction, which is committed once the last one succeeded and
    // dropped - rolling back the rows already written - on the first that fails.
    application::Result<std::unique_ptr<application::Transaction>> begun =
        controller->beginTransaction();
    if (!begun.ok())
        return QString::fromStdString(begun.error());
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    for (const Pending& record : pending) {
        const application::Status status =
            controller->recordValuation(record.id, month, record.balance);
        if (!status.ok()) {
            transaction.reset();
            return message(status);
        }
    }

    const application::Status committed = transaction->commit();
    return committed.ok() ? QString{} : message(committed);
}

QString AccountsViewModel::addDebt(const QString& name, int kindIndex, const QString& rateText,
                                   int styleIndex, const QString& paymentText,
                                   const QString& feeText, const QString& termYearsText,
                                   const QString& balanceText)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Give the debt a name.");

    // An unstated rate is zero, which is honest: the model reads a debt's rate
    // to decide whether it is worth paying down first, and a zero rate never
    // claims more than the household said.
    domain::InterestRate rate{};
    if (!rateText.trimmed().isEmpty()) {
        const InterestRateInput parsed = parseInterestRateInput(rateText);
        if (!parsed.ok)
            return parsed.error;
        rate = parsed.value;
    }

    const ParsedDebtTerms terms =
        readDebtTerms(styleIndex, paymentText, feeText, termYearsText, m_currency);
    if (!terms.ok)
        return terms.error;

    const MoneyInput amount = parseMoneyInput(balanceText, m_currency);
    if (!amount.ok)
        return amount.error;

    const domain::Liability liability = domain::makeLiability(
        newId().toStdString(), trimmed.toStdString(), liabilityKindAt(kindIndex), rate, terms.style,
        terms.payment, terms.monthlyFee, terms.termMonths);

    // The debt and the balance it was created with are one decision, so they are
    // written inside one transaction: a loan must not survive without its
    // opening figure, and a failed balance takes the loan with it.
    application::Result<std::unique_ptr<application::Transaction>> begun =
        controller->beginTransaction();
    if (!begun.ok())
        return QString::fromStdString(begun.error());
    std::unique_ptr<application::Transaction> transaction = std::move(begun.value());

    application::Status status = controller->addLiability(liability);
    if (status.ok())
        status = controller->recordDebtBalance(liability.id, AppController::currentMonth(),
                                               amount.value);
    if (status.ok())
        status = transaction->commit();

    if (!status.ok()) {
        // Dropping the transaction is what rolls the liability back; either way
        // the household is told the error the use case reported.
        transaction.reset();
        return message(status);
    }
    return {};
}

QString AccountsViewModel::updateDebt(const QString& liabilityId, const QString& name,
                                      int kindIndex, const QString& rateText, int styleIndex,
                                      const QString& paymentText, const QString& feeText,
                                      const QString& termYearsText)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("Give the debt a name.");

    const std::string id = liabilityId.toStdString();
    const std::vector<domain::Liability> existing = controller->liabilities();
    const auto found =
        std::find_if(existing.cbegin(), existing.cend(),
                     [&id](const domain::Liability& liability) { return liability.id == id; });
    if (found == existing.cend())
        return tr("That debt is no longer there. Reload the page.");

    domain::InterestRate rate{};
    if (!rateText.trimmed().isEmpty()) {
        const InterestRateInput parsed = parseInterestRateInput(rateText);
        if (!parsed.ok)
            return parsed.error;
        rate = parsed.value;
    }

    const ParsedDebtTerms terms =
        readDebtTerms(styleIndex, paymentText, feeText, termYearsText, m_currency);
    if (!terms.ok)
        return terms.error;

    // The same id goes back in, which is what keeps every balance recorded
    // against the loan: a rate change corrects the loan rather than ending it
    // and starting another one without its history.
    const domain::Liability updated =
        domain::makeLiability(found->id, trimmed.toStdString(), liabilityKindAt(kindIndex), rate,
                              terms.style, terms.payment, terms.monthlyFee, terms.termMonths);

    const application::Status status = controller->updateLiability(updated);
    return status.ok() ? QString{} : message(status);
}

QString AccountsViewModel::removeDebt(const QString& liabilityId)
{
    AppController* controller = AppController::instance();
    if (controller == nullptr)
        return tr("The database is not open.");

    const application::Status status = controller->removeLiability(liabilityId.toStdString());
    return status.ok() ? QString{} : message(status);
}

} // namespace hinkwise::presentation
