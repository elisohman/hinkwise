// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "repositories.h"

#include "domain/serialisation.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hinkwise::infrastructure::sqlite {

namespace {

// ---------------------------------------------------------------------------
// The statements. Each one is named, prepared, and bound from values; none is
// assembled from parts, so no stored text can become SQL.
// ---------------------------------------------------------------------------

constexpr const char* kSelectProfile = R"(
    SELECT display_name, base_currency, phase, risk_tolerance_bp, jurisdiction_id,
           safe_withdrawal_bp, capital_tax_bp, fi_multiplier_low, fi_multiplier_default,
           fi_multiplier_high, tracking_mode
    FROM profile
    WHERE id = 1
)";

// created_at is set on insert and left alone on update: it records when the
// household was created, not when it was last edited.
constexpr const char* kUpsertProfile = R"(
    INSERT INTO profile (id, display_name, base_currency, phase, risk_tolerance_bp,
                         jurisdiction_id, safe_withdrawal_bp, capital_tax_bp,
                         fi_multiplier_low, fi_multiplier_default, fi_multiplier_high,
                         tracking_mode, created_at)
    VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT (id) DO UPDATE SET
        display_name = excluded.display_name,
        base_currency = excluded.base_currency,
        phase = excluded.phase,
        risk_tolerance_bp = excluded.risk_tolerance_bp,
        jurisdiction_id = excluded.jurisdiction_id,
        safe_withdrawal_bp = excluded.safe_withdrawal_bp,
        capital_tax_bp = excluded.capital_tax_bp,
        fi_multiplier_low = excluded.fi_multiplier_low,
        fi_multiplier_default = excluded.fi_multiplier_default,
        fi_multiplier_high = excluded.fi_multiplier_high,
        tracking_mode = excluded.tracking_mode
)";

constexpr const char* kSelectAssets = R"(
    SELECT id, name, kind, bucket, liquid, included_in_headline
    FROM asset
    WHERE archived = 0
    ORDER BY name, id
)";

constexpr const char* kUpsertAsset = R"(
    INSERT INTO asset (id, name, kind, bucket, liquid, included_in_headline, archived)
    VALUES (?, ?, ?, ?, ?, ?, 0)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        kind = excluded.kind,
        bucket = excluded.bucket,
        liquid = excluded.liquid,
        included_in_headline = excluded.included_in_headline
)";

constexpr const char* kDeleteAsset = "DELETE FROM asset WHERE id = ?";

constexpr const char* kSelectLiabilities = R"(
    SELECT id, name, kind, interest_100k, repayment_style, regular_payment_minor,
           monthly_fee_minor, term_months,
           COALESCE((SELECT base_currency FROM profile WHERE id = 1), 'SEK')
    FROM liability
    WHERE archived = 0
    ORDER BY name, id
)";

constexpr const char* kUpsertLiability = R"(
    INSERT INTO liability (id, name, kind, interest_100k, repayment_style,
                           regular_payment_minor, monthly_fee_minor, term_months, archived)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        kind = excluded.kind,
        interest_100k = excluded.interest_100k,
        repayment_style = excluded.repayment_style,
        regular_payment_minor = excluded.regular_payment_minor,
        monthly_fee_minor = excluded.monthly_fee_minor,
        term_months = excluded.term_months
)";

constexpr const char* kDeleteLiability = "DELETE FROM liability WHERE id = ?";

constexpr const char* kSelectPlannedExpenses = R"(
    SELECT id, name, amount_minor, due_month, currency
    FROM planned_expense
    ORDER BY due_month, name, id
)";

constexpr const char* kUpsertPlannedExpense = R"(
    INSERT INTO planned_expense (id, name, amount_minor, due_month, currency)
    VALUES (?, ?, ?, ?, ?)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        amount_minor = excluded.amount_minor,
        due_month = excluded.due_month,
        currency = excluded.currency
)";

constexpr const char* kDeletePlannedExpense = "DELETE FROM planned_expense WHERE id = ?";

// "YYYY-MM" sorts chronologically as text, which is why the month needs no
// separate ordering column.
constexpr const char* kSelectValuation = R"(
    SELECT amount_minor, currency
    FROM valuation
    WHERE asset_id = ? AND month = ?
)";

constexpr const char* kSelectLatestValuation = R"(
    SELECT amount_minor, currency
    FROM valuation
    WHERE asset_id = ?
    ORDER BY month DESC
    LIMIT 1
)";

// "YYYY-MM" sorts chronologically as text, so ORDER BY month is the calendar
// order the history is read back in.
constexpr const char* kSelectValuationHistory = R"(
    SELECT month, amount_minor, currency
    FROM valuation
    WHERE asset_id = ?
    ORDER BY month ASC
)";

// "YYYY-MM" sorts chronologically as text, so MAX(month) is the latest month
// recorded and NULL when nothing has been. One query, no rows read.
constexpr const char* kSelectLatestValuationMonth = "SELECT MAX(month) FROM valuation";

constexpr const char* kUpsertValuation = R"(
    INSERT INTO valuation (asset_id, month, amount_minor, currency)
    VALUES (?, ?, ?, ?)
    ON CONFLICT (asset_id, month) DO UPDATE SET
        amount_minor = excluded.amount_minor,
        currency = excluded.currency
)";

constexpr const char* kSelectDebtBalance = R"(
    SELECT amount_minor, currency
    FROM debt_balance
    WHERE liability_id = ? AND month = ?
)";

constexpr const char* kSelectLatestDebtBalance = R"(
    SELECT amount_minor, currency
    FROM debt_balance
    WHERE liability_id = ?
    ORDER BY month DESC
    LIMIT 1
)";

constexpr const char* kSelectDebtBalanceHistory = R"(
    SELECT month, amount_minor, currency
    FROM debt_balance
    WHERE liability_id = ?
    ORDER BY month ASC
)";

constexpr const char* kSelectLatestDebtBalanceMonth = "SELECT MAX(month) FROM debt_balance";

constexpr const char* kUpsertDebtBalance = R"(
    INSERT INTO debt_balance (liability_id, month, amount_minor, currency)
    VALUES (?, ?, ?, ?)
    ON CONFLICT (liability_id, month) DO UPDATE SET
        amount_minor = excluded.amount_minor,
        currency = excluded.currency
)";

constexpr const char* kSelectMonthFlow = R"(
    SELECT income_minor, needs_minor, wants_minor, saving_minor, currency,
           saving_buffer_minor, saving_invest_minor, saving_amort_minor
    FROM month_flow
    WHERE month = ?
)";

constexpr const char* kSelectRecentMonthFlows = R"(
    SELECT month, income_minor, needs_minor, wants_minor, saving_minor, currency,
           saving_buffer_minor, saving_invest_minor, saving_amort_minor
    FROM month_flow
    ORDER BY month DESC
    LIMIT ?
)";

// The saving breakdown is three nullable columns: all NULL is a month with no
// recorded breakdown, and the repository refuses a row where only some are
// NULL. An absent breakdown is written back as NULL rather than as zeroes.
constexpr const char* kUpsertMonthFlow = R"(
    INSERT INTO month_flow (month, income_minor, needs_minor, wants_minor, saving_minor, currency,
                            saving_buffer_minor, saving_invest_minor, saving_amort_minor)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT (month) DO UPDATE SET
        income_minor = excluded.income_minor,
        needs_minor = excluded.needs_minor,
        wants_minor = excluded.wants_minor,
        saving_minor = excluded.saving_minor,
        currency = excluded.currency,
        saving_buffer_minor = excluded.saving_buffer_minor,
        saving_invest_minor = excluded.saving_invest_minor,
        saving_amort_minor = excluded.saving_amort_minor
)";

constexpr const char* kSelectPhaseHistory = R"(
    SELECT changed_at, phase
    FROM phase_history
    ORDER BY changed_at
)";

constexpr const char* kInsertPhaseHistory =
    "INSERT INTO phase_history (changed_at, phase) VALUES (?, ?)";

// ---------------------------------------------------------------------------
// Small conversions shared by every repository.
// ---------------------------------------------------------------------------

std::string toStd(const QString& text)
{
    return text.toStdString();
}

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string queryFailure(std::string_view what, const QSqlQuery& query)
{
    return std::string{what} + ": " + toStd(query.lastError().text());
}

std::string unknownKey(const std::string& context, std::string_view field, const std::string& key)
{
    return context + ": unknown " + std::string{field} + " '" + key + "'";
}

/// One prepared statement, kept on the stack: QSqlQuery is not copyable, and
/// every statement here is used once.
class Statement {
public:
    Statement(const QSqlDatabase& connection, const char* sql, std::string_view what)
        : m_query{connection}
        , m_what(what)
    {
        if (!m_query.prepare(QString::fromUtf8(sql)))
            m_error = queryFailure(m_what, m_query);
    }

    /// Binds `values` in order and runs the statement.
    application::Status exec(const QVariantList& values = {})
    {
        if (!m_error.empty())
            return application::Status::err(m_error);

        for (const QVariant& value : values)
            m_query.addBindValue(value);
        if (!m_query.exec())
            return application::Status::err(queryFailure(m_what, m_query));
        return application::Status::success();
    }

    bool next() { return m_query.next(); }
    QVariant value(int column) const { return m_query.value(column); }

private:
    QSqlQuery m_query;
    std::string m_what;
    std::string m_error;
};

application::Result<domain::Currency> decodeCurrency(const QVariant& value,
                                                     const std::string& context)
{
    const std::string key = toStd(value.toString());
    const std::optional<domain::Currency> currency = domain::currencyFromStorageKey(key);
    if (!currency)
        return application::Result<domain::Currency>::err(unknownKey(context, "currency", key));
    return application::Result<domain::Currency>::ok(*currency);
}

application::Result<domain::Money>
decodeMoney(const QVariant& minorUnits, const QVariant& currencyColumn, const std::string& context)
{
    const application::Result<domain::Currency> currency = decodeCurrency(currencyColumn, context);
    if (!currency.ok())
        return application::Result<domain::Money>::err(currency.error());
    return application::Result<domain::Money>::ok(
        domain::Money{minorUnits.toLongLong(), currency.value()});
}

application::Result<domain::YearMonth> decodeMonth(const QVariant& value,
                                                   const std::string& context)
{
    const std::string text = toStd(value.toString());
    const std::optional<domain::YearMonth> month = domain::YearMonth::parse(text);
    if (!month)
        return application::Result<domain::YearMonth>::err(context + ": bad month '" + text +
                                                           "' (expected YYYY-MM)");
    return application::Result<domain::YearMonth>::ok(*month);
}

QString monthText(domain::YearMonth month)
{
    return toQString(month.toString());
}

/// MAX(month) over an empty table is NULL, which is "never recorded" rather
/// than a month that failed to parse.
application::Result<std::optional<domain::YearMonth>> decodeLatestMonth(const QVariant& value,
                                                                        const std::string& context)
{
    if (value.isNull())
        return application::Result<std::optional<domain::YearMonth>>::ok(std::nullopt);

    const application::Result<domain::YearMonth> month = decodeMonth(value, context);
    if (!month.ok())
        return application::Result<std::optional<domain::YearMonth>>::err(month.error());
    return application::Result<std::optional<domain::YearMonth>>::ok(month.value());
}

QString phaseText(domain::Phase phase)
{
    return QString::fromUtf8(domain::storageKey(phase));
}

std::string nowIso()
{
    return toStd(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

application::Result<domain::Phase> decodePhase(const QVariant& value, const std::string& context)
{
    const std::string key = toStd(value.toString());
    const std::optional<domain::Phase> phase = domain::phaseFromStorageKey(key);
    if (!phase)
        return application::Result<domain::Phase>::err(unknownKey(context, "phase", key));
    return application::Result<domain::Phase>::ok(*phase);
}

application::Result<domain::TrackingMode> decodeTrackingMode(const QVariant& value,
                                                             const std::string& context)
{
    const std::string key = toStd(value.toString());
    const std::optional<domain::TrackingMode> mode = domain::trackingModeFromStorageKey(key);
    if (!mode)
        return application::Result<domain::TrackingMode>::err(
            unknownKey(context, "tracking mode", key));
    return application::Result<domain::TrackingMode>::ok(*mode);
}

application::Result<domain::MonthFlow> decodeFlow(const Statement& row, int firstColumn,
                                                  const std::string& context)
{
    const application::Result<domain::Currency> currency =
        decodeCurrency(row.value(firstColumn + 4), context);
    if (!currency.ok())
        return application::Result<domain::MonthFlow>::err(currency.error());

    domain::MonthFlow flow;
    flow.income = domain::Money{row.value(firstColumn).toLongLong(), currency.value()};
    flow.needs = domain::Money{row.value(firstColumn + 1).toLongLong(), currency.value()};
    flow.wants = domain::Money{row.value(firstColumn + 2).toLongLong(), currency.value()};
    flow.saving = domain::Money{row.value(firstColumn + 3).toLongLong(), currency.value()};

    const QVariant bufferColumn = row.value(firstColumn + 5);
    const QVariant investColumn = row.value(firstColumn + 6);
    const QVariant amortColumn = row.value(firstColumn + 7);
    const int storedParts = (bufferColumn.isNull() ? 0 : 1) + (investColumn.isNull() ? 0 : 1) +
                            (amortColumn.isNull() ? 0 : 1);

    if (storedParts == 3) {
        flow.savingSplit = domain::SavingsBreakdown{
            domain::Money{bufferColumn.toLongLong(), currency.value()},
            domain::Money{investColumn.toLongLong(), currency.value()},
            domain::Money{amortColumn.toLongLong(), currency.value()},
        };
        if (!domain::savingSplitMatchesTotal(flow))
            return application::Result<domain::MonthFlow>::err(
                context + ": the saving breakdown adds up to " +
                flow.savingSplit->total().toString() + " but saving is " + flow.saving.toString());
    } else if (storedParts != 0) {
        // Some of the three columns are filled in and some are not: no writer
        // produces this, so the row was hand-edited or half written. Reading it
        // as "no breakdown" would throw away the parts that are there.
        return application::Result<domain::MonthFlow>::err(
            context + ": saving breakdown is stored for only some of its parts; buffer, "
                      "investing and amortisation are either all recorded or all absent");
    }

    return application::Result<domain::MonthFlow>::ok(flow);
}

} // namespace

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------

SqliteProfileRepository::SqliteProfileRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::optional<domain::Profile>> SqliteProfileRepository::load() const
{
    using Outcome = application::Result<std::optional<domain::Profile>>;

    Statement statement{m_connection, kSelectProfile, "load profile"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    domain::Profile profile;
    profile.displayName = toStd(statement.value(0).toString());

    const application::Result<domain::Currency> currency =
        decodeCurrency(statement.value(1), "profile");
    if (!currency.ok())
        return Outcome::err(currency.error());
    profile.baseCurrency = currency.value();

    const application::Result<domain::Phase> phase = decodePhase(statement.value(2), "profile");
    if (!phase.ok())
        return Outcome::err(phase.error());
    profile.phase = phase.value();

    profile.riskTolerance = domain::Percentage::fromBasisPoints(statement.value(3).toInt());

    domain::Jurisdiction jurisdiction;
    jurisdiction.id = toStd(statement.value(4).toString());
    jurisdiction.safeWithdrawalRate =
        domain::Percentage::fromBasisPoints(statement.value(5).toInt());
    jurisdiction.capitalTaxDrag = domain::Percentage::fromBasisPoints(statement.value(6).toInt());
    jurisdiction.fiMultiplierLow = statement.value(7).toInt();
    jurisdiction.fiMultiplierDefault = statement.value(8).toInt();
    jurisdiction.fiMultiplierHigh = statement.value(9).toInt();
    profile.jurisdiction = jurisdiction;

    const application::Result<domain::TrackingMode> trackingMode =
        decodeTrackingMode(statement.value(10), "profile");
    if (!trackingMode.ok())
        return Outcome::err(trackingMode.error());
    profile.trackingMode = trackingMode.value();

    return Outcome::ok(profile);
}

application::Status SqliteProfileRepository::save(const domain::Profile& profile)
{
    return Statement{m_connection, kUpsertProfile, "save profile"}.exec({
        toQString(profile.displayName),
        QString::fromUtf8(domain::storageKey(profile.baseCurrency)),
        phaseText(profile.phase),
        profile.riskTolerance.basisPoints(),
        toQString(profile.jurisdiction.id),
        profile.jurisdiction.safeWithdrawalRate.basisPoints(),
        profile.jurisdiction.capitalTaxDrag.basisPoints(),
        profile.jurisdiction.fiMultiplierLow,
        profile.jurisdiction.fiMultiplierDefault,
        profile.jurisdiction.fiMultiplierHigh,
        QString::fromUtf8(domain::storageKey(profile.trackingMode)),
        toQString(nowIso()),
    });
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------

SqliteAssetRepository::SqliteAssetRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::vector<domain::Asset>> SqliteAssetRepository::all() const
{
    using Outcome = application::Result<std::vector<domain::Asset>>;

    Statement statement{m_connection, kSelectAssets, "load assets"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<domain::Asset> assets;
    while (statement.next()) {
        domain::Asset asset;
        asset.id = toStd(statement.value(0).toString());
        asset.name = toStd(statement.value(1).toString());

        const std::string kindKey = toStd(statement.value(2).toString());
        const std::optional<domain::AssetKind> kind = domain::assetKindFromStorageKey(kindKey);
        if (!kind)
            return Outcome::err(unknownKey("asset '" + asset.id + "'", "kind", kindKey));
        asset.kind = *kind;

        const std::string bucketKey = toStd(statement.value(3).toString());
        const std::optional<domain::Bucket> bucket = domain::bucketFromStorageKey(bucketKey);
        if (!bucket)
            return Outcome::err(unknownKey("asset '" + asset.id + "'", "bucket", bucketKey));
        asset.bucket = *bucket;

        asset.liquid = statement.value(4).toInt() != 0;
        asset.includedInHeadline = statement.value(5).toInt() != 0;
        assets.push_back(std::move(asset));
    }
    return Outcome::ok(std::move(assets));
}

application::Status SqliteAssetRepository::upsert(const domain::Asset& asset)
{
    return Statement{m_connection, kUpsertAsset, "save asset"}.exec({
        toQString(asset.id),
        toQString(asset.name),
        QString::fromUtf8(domain::storageKey(asset.kind)),
        QString::fromUtf8(domain::storageKey(asset.bucket)),
        asset.liquid ? 1 : 0,
        asset.includedInHeadline ? 1 : 0,
    });
}

application::Status SqliteAssetRepository::remove(std::string_view id)
{
    return Statement{m_connection, kDeleteAsset, "delete asset"}.exec({toQString(id)});
}

// ---------------------------------------------------------------------------
// Liabilities
// ---------------------------------------------------------------------------

SqliteLiabilityRepository::SqliteLiabilityRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::vector<domain::Liability>> SqliteLiabilityRepository::all() const
{
    using Outcome = application::Result<std::vector<domain::Liability>>;

    Statement statement{m_connection, kSelectLiabilities, "load liabilities"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<domain::Liability> liabilities;
    while (statement.next()) {
        domain::Liability liability;
        liability.id = toStd(statement.value(0).toString());
        liability.name = toStd(statement.value(1).toString());

        const std::string kindKey = toStd(statement.value(2).toString());
        const std::optional<domain::LiabilityKind> kind =
            domain::liabilityKindFromStorageKey(kindKey);
        if (!kind)
            return Outcome::err(unknownKey("liability '" + liability.id + "'", "kind", kindKey));
        liability.kind = *kind;

        liability.interestRate = domain::InterestRate::fromPartsPer100k(statement.value(3).toInt());

        const std::string styleKey = toStd(statement.value(4).toString());
        const std::optional<domain::RepaymentStyle> style =
            domain::repaymentStyleFromStorageKey(styleKey);
        if (!style)
            return Outcome::err(
                unknownKey("liability '" + liability.id + "'", "repayment style", styleKey));
        liability.style = *style;

        // A liability carries no currency of its own: the household has one
        // base currency, and its payments are amounts in it.
        const std::string currencyKey = toStd(statement.value(8).toString());
        const std::optional<domain::Currency> currency =
            domain::currencyFromStorageKey(currencyKey);
        if (!currency)
            return Outcome::err(
                unknownKey("liability '" + liability.id + "'", "currency", currencyKey));
        liability.regularPayment = domain::Money{statement.value(5).toLongLong(), *currency};
        liability.monthlyFee = domain::Money{statement.value(6).toLongLong(), *currency};
        liability.termMonths = statement.value(7).toInt();
        liabilities.push_back(std::move(liability));
    }
    return Outcome::ok(std::move(liabilities));
}

application::Status SqliteLiabilityRepository::upsert(const domain::Liability& liability)
{
    return Statement{m_connection, kUpsertLiability, "save liability"}.exec({
        toQString(liability.id),
        toQString(liability.name),
        QString::fromUtf8(domain::storageKey(liability.kind)),
        liability.interestRate.partsPer100k(),
        QString::fromUtf8(domain::storageKey(liability.style)),
        static_cast<qlonglong>(liability.regularPayment.minorUnits()),
        static_cast<qlonglong>(liability.monthlyFee.minorUnits()),
        liability.termMonths,
    });
}

application::Status SqliteLiabilityRepository::remove(std::string_view id)
{
    return Statement{m_connection, kDeleteLiability, "delete liability"}.exec({toQString(id)});
}

// ---------------------------------------------------------------------------
// Planned expenses
// ---------------------------------------------------------------------------

SqlitePlannedExpenseRepository::SqlitePlannedExpenseRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::vector<domain::PlannedExpense>> SqlitePlannedExpenseRepository::all() const
{
    using Outcome = application::Result<std::vector<domain::PlannedExpense>>;

    Statement statement{m_connection, kSelectPlannedExpenses, "load planned expenses"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<domain::PlannedExpense> expenses;
    while (statement.next()) {
        domain::PlannedExpense expense;
        expense.id = toStd(statement.value(0).toString());
        expense.name = toStd(statement.value(1).toString());

        const std::string context = "planned expense '" + expense.id + "'";
        const application::Result<domain::Money> amount =
            decodeMoney(statement.value(2), statement.value(4), context);
        if (!amount.ok())
            return Outcome::err(amount.error());
        expense.amount = amount.value();

        const application::Result<domain::YearMonth> dueMonth =
            decodeMonth(statement.value(3), context);
        if (!dueMonth.ok())
            return Outcome::err(dueMonth.error());
        expense.dueMonth = dueMonth.value();

        expenses.push_back(std::move(expense));
    }
    return Outcome::ok(std::move(expenses));
}

application::Status SqlitePlannedExpenseRepository::upsert(const domain::PlannedExpense& expense)
{
    return Statement{m_connection, kUpsertPlannedExpense, "save planned expense"}.exec({
        toQString(expense.id),
        toQString(expense.name),
        static_cast<qint64>(expense.amount.minorUnits()),
        monthText(expense.dueMonth),
        QString::fromUtf8(domain::storageKey(expense.amount.currency())),
    });
}

application::Status SqlitePlannedExpenseRepository::remove(std::string_view id)
{
    return Statement{m_connection, kDeletePlannedExpense, "delete planned expense"}.exec(
        {toQString(id)});
}

// ---------------------------------------------------------------------------
// Asset valuations
// ---------------------------------------------------------------------------

SqliteValuationRepository::SqliteValuationRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::optional<domain::Money>>
SqliteValuationRepository::balanceAt(std::string_view assetId, domain::YearMonth month) const
{
    using Outcome = application::Result<std::optional<domain::Money>>;

    Statement statement{m_connection, kSelectValuation, "load valuation"};
    const application::Status ran = statement.exec({toQString(assetId), monthText(month)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const application::Result<domain::Money> amount =
        decodeMoney(statement.value(0), statement.value(1),
                    "valuation of '" + toStd(toQString(assetId)) + "' in " + month.toString());
    if (!amount.ok())
        return Outcome::err(amount.error());
    return Outcome::ok(amount.value());
}

application::Result<std::optional<domain::Money>>
SqliteValuationRepository::latest(std::string_view assetId) const
{
    using Outcome = application::Result<std::optional<domain::Money>>;

    Statement statement{m_connection, kSelectLatestValuation, "load latest valuation"};
    const application::Status ran = statement.exec({toQString(assetId)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const application::Result<domain::Money> amount =
        decodeMoney(statement.value(0), statement.value(1),
                    "latest valuation of '" + toStd(toQString(assetId)) + "'");
    if (!amount.ok())
        return Outcome::err(amount.error());
    return Outcome::ok(amount.value());
}

application::Result<std::vector<application::MoneyPoint>>
SqliteValuationRepository::history(std::string_view assetId) const
{
    using Outcome = application::Result<std::vector<application::MoneyPoint>>;

    Statement statement{m_connection, kSelectValuationHistory, "load valuation history"};
    const application::Status ran = statement.exec({toQString(assetId)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<application::MoneyPoint> points;
    while (statement.next()) {
        const application::Result<domain::YearMonth> month =
            decodeMonth(statement.value(0), "valuation of '" + toStd(toQString(assetId)) + "'");
        if (!month.ok())
            return Outcome::err(month.error());

        const application::Result<domain::Money> amount = decodeMoney(
            statement.value(1), statement.value(2),
            "valuation of '" + toStd(toQString(assetId)) + "' in " + month.value().toString());
        if (!amount.ok())
            return Outcome::err(amount.error());
        points.push_back(application::MoneyPoint{month.value(), amount.value()});
    }
    return Outcome::ok(std::move(points));
}

application::Status SqliteValuationRepository::upsert(std::string_view assetId,
                                                      domain::YearMonth month,
                                                      domain::Money balance)
{
    return Statement{m_connection, kUpsertValuation, "save valuation"}.exec({
        toQString(assetId),
        monthText(month),
        static_cast<qint64>(balance.minorUnits()),
        QString::fromUtf8(domain::storageKey(balance.currency())),
    });
}

application::Result<std::optional<domain::YearMonth>>
SqliteValuationRepository::mostRecentMonth() const
{
    using Outcome = application::Result<std::optional<domain::YearMonth>>;

    Statement statement{m_connection, kSelectLatestValuationMonth, "load latest valuation month"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);
    return decodeLatestMonth(statement.value(0), "latest valuation month");
}

// ---------------------------------------------------------------------------
// Debt balances
// ---------------------------------------------------------------------------

SqliteDebtBalanceRepository::SqliteDebtBalanceRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::optional<domain::Money>>
SqliteDebtBalanceRepository::balanceAt(std::string_view liabilityId, domain::YearMonth month) const
{
    using Outcome = application::Result<std::optional<domain::Money>>;

    Statement statement{m_connection, kSelectDebtBalance, "load debt balance"};
    const application::Status ran = statement.exec({toQString(liabilityId), monthText(month)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const application::Result<domain::Money> amount = decodeMoney(
        statement.value(0), statement.value(1),
        "debt balance of '" + toStd(toQString(liabilityId)) + "' in " + month.toString());
    if (!amount.ok())
        return Outcome::err(amount.error());
    return Outcome::ok(amount.value());
}

application::Result<std::optional<domain::Money>>
SqliteDebtBalanceRepository::latest(std::string_view liabilityId) const
{
    using Outcome = application::Result<std::optional<domain::Money>>;

    Statement statement{m_connection, kSelectLatestDebtBalance, "load latest debt balance"};
    const application::Status ran = statement.exec({toQString(liabilityId)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const application::Result<domain::Money> amount =
        decodeMoney(statement.value(0), statement.value(1),
                    "latest debt balance of '" + toStd(toQString(liabilityId)) + "'");
    if (!amount.ok())
        return Outcome::err(amount.error());
    return Outcome::ok(amount.value());
}

application::Result<std::vector<application::MoneyPoint>>
SqliteDebtBalanceRepository::history(std::string_view liabilityId) const
{
    using Outcome = application::Result<std::vector<application::MoneyPoint>>;

    Statement statement{m_connection, kSelectDebtBalanceHistory, "load debt balance history"};
    const application::Status ran = statement.exec({toQString(liabilityId)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<application::MoneyPoint> points;
    while (statement.next()) {
        const application::Result<domain::YearMonth> month = decodeMonth(
            statement.value(0), "debt balance of '" + toStd(toQString(liabilityId)) + "'");
        if (!month.ok())
            return Outcome::err(month.error());

        const application::Result<domain::Money> amount =
            decodeMoney(statement.value(1), statement.value(2),
                        "debt balance of '" + toStd(toQString(liabilityId)) + "' in " +
                            month.value().toString());
        if (!amount.ok())
            return Outcome::err(amount.error());
        points.push_back(application::MoneyPoint{month.value(), amount.value()});
    }
    return Outcome::ok(std::move(points));
}

application::Status SqliteDebtBalanceRepository::upsert(std::string_view liabilityId,
                                                        domain::YearMonth month,
                                                        domain::Money balance)
{
    return Statement{m_connection, kUpsertDebtBalance, "save debt balance"}.exec({
        toQString(liabilityId),
        monthText(month),
        static_cast<qint64>(balance.minorUnits()),
        QString::fromUtf8(domain::storageKey(balance.currency())),
    });
}

application::Result<std::optional<domain::YearMonth>>
SqliteDebtBalanceRepository::mostRecentMonth() const
{
    using Outcome = application::Result<std::optional<domain::YearMonth>>;

    Statement statement{m_connection, kSelectLatestDebtBalanceMonth,
                        "load latest debt balance month"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);
    return decodeLatestMonth(statement.value(0), "latest debt balance month");
}

// ---------------------------------------------------------------------------
// Month flows
// ---------------------------------------------------------------------------

SqliteMonthFlowRepository::SqliteMonthFlowRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::optional<domain::MonthFlow>>
SqliteMonthFlowRepository::at(domain::YearMonth month) const
{
    using Outcome = application::Result<std::optional<domain::MonthFlow>>;

    Statement statement{m_connection, kSelectMonthFlow, "load month flow"};
    const application::Status ran = statement.exec({monthText(month)});
    if (!ran.ok())
        return Outcome::err(ran.error());

    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const application::Result<domain::MonthFlow> flow =
        decodeFlow(statement, 0, "month flow " + month.toString());
    if (!flow.ok())
        return Outcome::err(flow.error());
    return Outcome::ok(flow.value());
}

application::Result<std::vector<application::MonthFlowRecord>>
SqliteMonthFlowRepository::recent(int count) const
{
    using Outcome = application::Result<std::vector<application::MonthFlowRecord>>;

    if (count < 0)
        return Outcome::err("load recent month flows: count must not be negative");

    Statement statement{m_connection, kSelectRecentMonthFlows, "load recent month flows"};
    const application::Status ran = statement.exec({count});
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<application::MonthFlowRecord> records;
    std::optional<domain::Currency> onlyCurrency;
    std::string firstMonthWithCurrency;

    while (statement.next()) {
        const std::string monthKey = toStd(statement.value(0).toString());
        const application::Result<domain::YearMonth> month =
            decodeMonth(statement.value(0), "month flow " + monthKey);
        if (!month.ok())
            return Outcome::err(month.error());

        const application::Result<domain::MonthFlow> flow =
            decodeFlow(statement, 1, "month flow " + monthKey);
        if (!flow.ok())
            return Outcome::err(flow.error());

        // One household has one currency. Several months in different
        // currencies is data from before a currency switch, or a hand-edited
        // file; either way a trend line across them would be a lie, and adding
        // the amounts would throw.
        const domain::Currency currency = flow.value().income.currency();
        if (!onlyCurrency) {
            onlyCurrency = currency;
            firstMonthWithCurrency = monthKey;
        } else if (*onlyCurrency != currency) {
            return Outcome::err("month flows " + firstMonthWithCurrency + " and " + monthKey +
                                " are stored in both " + domain::storageKey(*onlyCurrency) +
                                " and " + domain::storageKey(currency));
        }

        records.push_back(application::MonthFlowRecord{month.value(), flow.value()});
    }
    return Outcome::ok(std::move(records));
}

application::Status SqliteMonthFlowRepository::upsert(domain::YearMonth month,
                                                      const domain::MonthFlow& flow)
{
    const std::string context = "save month flow " + month.toString();
    const domain::Currency currency = flow.income.currency();

    std::array<const domain::Money*, 6> amounts{&flow.needs, &flow.wants, &flow.saving};
    std::size_t amountCount = 3;
    if (flow.savingSplit) {
        amounts[3] = &flow.savingSplit->toBuffer;
        amounts[4] = &flow.savingSplit->toInvesting;
        amounts[5] = &flow.savingSplit->toAmortisation;
        amountCount = amounts.size();
    }

    for (std::size_t i = 0; i < amountCount; ++i) {
        const domain::Money& amount = *amounts[i];
        if (amount.currency() != currency)
            return application::Status::err(
                context + ": income is in " + domain::storageKey(currency) +
                " but another amount is in " + domain::storageKey(amount.currency()));
    }

    // The split is a breakdown of `saving`, so a split that does not add up to
    // it is refused here rather than stored and refused on the next read.
    if (!domain::savingSplitMatchesTotal(flow))
        return application::Status::err(context + ": the saving breakdown adds up to " +
                                        flow.savingSplit->total().toString() + " but saving is " +
                                        flow.saving.toString());

    QVariant buffer;
    QVariant investing;
    QVariant amortisation;
    if (flow.savingSplit) {
        buffer = static_cast<qint64>(flow.savingSplit->toBuffer.minorUnits());
        investing = static_cast<qint64>(flow.savingSplit->toInvesting.minorUnits());
        amortisation = static_cast<qint64>(flow.savingSplit->toAmortisation.minorUnits());
    }

    return Statement{m_connection, kUpsertMonthFlow, "save month flow"}.exec({
        monthText(month),
        static_cast<qint64>(flow.income.minorUnits()),
        static_cast<qint64>(flow.needs.minorUnits()),
        static_cast<qint64>(flow.wants.minorUnits()),
        static_cast<qint64>(flow.saving.minorUnits()),
        QString::fromUtf8(domain::storageKey(currency)),
        buffer,
        investing,
        amortisation,
    });
}

// ---------------------------------------------------------------------------
// Phase history
// ---------------------------------------------------------------------------

SqlitePhaseHistoryRepository::SqlitePhaseHistoryRepository(QSqlDatabase connection)
    : m_connection(std::move(connection))
{
}

application::Result<std::vector<application::PhaseChange>> SqlitePhaseHistoryRepository::all() const
{
    using Outcome = application::Result<std::vector<application::PhaseChange>>;

    Statement statement{m_connection, kSelectPhaseHistory, "load phase history"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<application::PhaseChange> changes;
    while (statement.next()) {
        application::PhaseChange change;
        change.changedAt = toStd(statement.value(0).toString());

        const application::Result<domain::Phase> phase =
            decodePhase(statement.value(1), "phase change at " + change.changedAt);
        if (!phase.ok())
            return Outcome::err(phase.error());
        change.phase = phase.value();

        changes.push_back(std::move(change));
    }
    return Outcome::ok(std::move(changes));
}

application::Status SqlitePhaseHistoryRepository::append(const application::PhaseChange& change)
{
    return Statement{m_connection, kInsertPhaseHistory, "append phase change"}.exec(
        {toQString(change.changedAt), phaseText(change.phase)});
}

} // namespace hinkwise::infrastructure::sqlite
