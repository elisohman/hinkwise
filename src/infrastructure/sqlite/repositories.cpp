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
    SELECT id, display_name, base_currency, phase, risk_tolerance_bp, jurisdiction_id,
           safe_withdrawal_bp, capital_tax_bp, fi_multiplier_low, fi_multiplier_default,
           fi_multiplier_high, tracking_mode, is_example, created_at
    FROM profile
    WHERE id = ?
)";

constexpr const char* kSelectProfiles = R"(
    SELECT id, display_name, base_currency, tracking_mode, phase, is_example
    FROM profile
    ORDER BY id
)";

constexpr const char* kSelectActiveProfileId =
    "SELECT value FROM meta WHERE key = 'active_profile'";

constexpr const char* kSetActiveProfileId = R"(
    INSERT INTO meta (key, value) VALUES ('active_profile', ?)
    ON CONFLICT (key) DO UPDATE SET value = excluded.value
)";

constexpr const char* kSelectProfileExists = "SELECT 1 FROM profile WHERE id = ?";

constexpr const char* kSelectProfileCount = "SELECT count(*) FROM profile";

constexpr const char* kSelectFirstProfileId = "SELECT id FROM profile ORDER BY id LIMIT 1";

// created_at is set on insert and left alone on update: it records when the
// household was created, not when it was last edited.
constexpr const char* kInsertProfile = R"(
    INSERT INTO profile (display_name, base_currency, phase, risk_tolerance_bp,
                         jurisdiction_id, safe_withdrawal_bp, capital_tax_bp,
                         fi_multiplier_low, fi_multiplier_default, fi_multiplier_high,
                         tracking_mode, is_example, created_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
)";

constexpr const char* kUpdateProfile = R"(
    UPDATE profile SET
        display_name = ?,
        base_currency = ?,
        phase = ?,
        risk_tolerance_bp = ?,
        jurisdiction_id = ?,
        safe_withdrawal_bp = ?,
        capital_tax_bp = ?,
        fi_multiplier_low = ?,
        fi_multiplier_default = ?,
        fi_multiplier_high = ?,
        tracking_mode = ?,
        is_example = ?
    WHERE id = ?
)";

constexpr const char* kDeleteProfile = "DELETE FROM profile WHERE id = ?";

constexpr const char* kSelectAssets = R"(
    SELECT id, name, kind, bucket, liquid, included_in_headline
    FROM asset
    WHERE archived = 0 AND profile_id = ?
    ORDER BY name, id
)";

constexpr const char* kUpsertAsset = R"(
    INSERT INTO asset (id, name, kind, bucket, liquid, included_in_headline, archived, profile_id)
    VALUES (?, ?, ?, ?, ?, ?, 0, ?)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        kind = excluded.kind,
        bucket = excluded.bucket,
        liquid = excluded.liquid,
        included_in_headline = excluded.included_in_headline,
        profile_id = excluded.profile_id
    WHERE profile_id = excluded.profile_id
)";

constexpr const char* kDeleteAsset = "DELETE FROM asset WHERE id = ? AND profile_id = ?";

constexpr const char* kSelectLiabilities = R"(
    SELECT id, name, kind, interest_100k, repayment_style, regular_payment_minor,
           monthly_fee_minor, term_months,
           COALESCE((SELECT base_currency FROM profile WHERE id = liability.profile_id), 'SEK')
    FROM liability
    WHERE archived = 0 AND profile_id = ?
    ORDER BY name, id
)";

constexpr const char* kUpsertLiability = R"(
    INSERT INTO liability (id, name, kind, interest_100k, repayment_style,
                           regular_payment_minor, monthly_fee_minor, term_months, archived,
                           profile_id)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, ?)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        kind = excluded.kind,
        interest_100k = excluded.interest_100k,
        repayment_style = excluded.repayment_style,
        regular_payment_minor = excluded.regular_payment_minor,
        monthly_fee_minor = excluded.monthly_fee_minor,
        term_months = excluded.term_months,
        profile_id = excluded.profile_id
    WHERE profile_id = excluded.profile_id
)";

constexpr const char* kDeleteLiability = "DELETE FROM liability WHERE id = ? AND profile_id = ?";

constexpr const char* kSelectPlannedExpenses = R"(
    SELECT id, name, amount_minor, due_month, currency
    FROM planned_expense
    WHERE profile_id = ?
    ORDER BY due_month, name, id
)";

constexpr const char* kUpsertPlannedExpense = R"(
    INSERT INTO planned_expense (id, name, amount_minor, due_month, currency, profile_id)
    VALUES (?, ?, ?, ?, ?, ?)
    ON CONFLICT (id) DO UPDATE SET
        name = excluded.name,
        amount_minor = excluded.amount_minor,
        due_month = excluded.due_month,
        currency = excluded.currency,
        profile_id = excluded.profile_id
    WHERE profile_id = excluded.profile_id
)";

constexpr const char* kDeletePlannedExpense =
    "DELETE FROM planned_expense WHERE id = ? AND profile_id = ?";

// "YYYY-MM" sorts chronologically as text, which is why the month needs no
// separate ordering column. Every valuation read joins its holding so another
// household's asset id can never return history.
constexpr const char* kSelectValuation = R"(
    SELECT v.amount_minor, v.currency
    FROM valuation v
    JOIN asset a ON a.id = v.asset_id
    WHERE v.asset_id = ? AND v.month = ? AND a.profile_id = ?
)";

constexpr const char* kSelectLatestValuation = R"(
    SELECT v.amount_minor, v.currency
    FROM valuation v
    JOIN asset a ON a.id = v.asset_id
    WHERE v.asset_id = ? AND a.profile_id = ?
    ORDER BY v.month DESC
    LIMIT 1
)";

// "YYYY-MM" sorts chronologically as text, so ORDER BY month is the calendar
// order the history is read back in.
constexpr const char* kSelectValuationHistory = R"(
    SELECT v.month, v.amount_minor, v.currency
    FROM valuation v
    JOIN asset a ON a.id = v.asset_id
    WHERE v.asset_id = ? AND a.profile_id = ?
    ORDER BY v.month ASC
)";

// "YYYY-MM" sorts chronologically as text, so MAX(month) is the latest month
// recorded and NULL when nothing has been. The join is what keeps another
// household's recent valuation from making this household's balances look
// fresh.
constexpr const char* kSelectLatestValuationMonth = R"(
    SELECT MAX(v.month)
    FROM valuation v
    JOIN asset a ON a.id = v.asset_id
    WHERE a.profile_id = ?
)";

// The insert only matches when the parent asset belongs to the active profile;
// otherwise it writes no row and the caller reports the refused write. The
// upsert then corrects the month or stores it for the first time.
constexpr const char* kUpsertValuation = R"(
    INSERT INTO valuation (asset_id, month, amount_minor, currency)
    SELECT ?, ?, ?, ?
    WHERE EXISTS (SELECT 1 FROM asset WHERE id = ? AND profile_id = ?)
    ON CONFLICT (asset_id, month) DO UPDATE SET
        amount_minor = excluded.amount_minor,
        currency = excluded.currency
)";

constexpr const char* kSelectDebtBalance = R"(
    SELECT b.amount_minor, b.currency
    FROM debt_balance b
    JOIN liability l ON l.id = b.liability_id
    WHERE b.liability_id = ? AND b.month = ? AND l.profile_id = ?
)";

constexpr const char* kSelectAssetOwner = "SELECT profile_id FROM asset WHERE id = ?";
constexpr const char* kSelectLiabilityOwner = "SELECT profile_id FROM liability WHERE id = ?";

constexpr const char* kSelectLatestDebtBalance = R"(
    SELECT b.amount_minor, b.currency
    FROM debt_balance b
    JOIN liability l ON l.id = b.liability_id
    WHERE b.liability_id = ? AND l.profile_id = ?
    ORDER BY b.month DESC
    LIMIT 1
)";

constexpr const char* kSelectDebtBalanceHistory = R"(
    SELECT b.month, b.amount_minor, b.currency
    FROM debt_balance b
    JOIN liability l ON l.id = b.liability_id
    WHERE b.liability_id = ? AND l.profile_id = ?
    ORDER BY b.month ASC
)";

constexpr const char* kSelectLatestDebtBalanceMonth = R"(
    SELECT MAX(b.month)
    FROM debt_balance b
    JOIN liability l ON l.id = b.liability_id
    WHERE l.profile_id = ?
)";

constexpr const char* kUpsertDebtBalance = R"(
    INSERT INTO debt_balance (liability_id, month, amount_minor, currency)
    SELECT ?, ?, ?, ?
    WHERE EXISTS (SELECT 1 FROM liability WHERE id = ? AND profile_id = ?)
    ON CONFLICT (liability_id, month) DO UPDATE SET
        amount_minor = excluded.amount_minor,
        currency = excluded.currency
)";

constexpr const char* kSelectMonthFlow = R"(
    SELECT income_minor, needs_minor, wants_minor, saving_minor, currency,
           saving_buffer_minor, saving_invest_minor, saving_amort_minor
    FROM month_flow
    WHERE profile_id = ? AND month = ?
)";

constexpr const char* kSelectRecentMonthFlows = R"(
    SELECT month, income_minor, needs_minor, wants_minor, saving_minor, currency,
           saving_buffer_minor, saving_invest_minor, saving_amort_minor
    FROM month_flow
    WHERE profile_id = ?
    ORDER BY month DESC
    LIMIT ?
)";

// The saving breakdown is three nullable columns: all NULL is a month with no
// recorded breakdown, and the repository refuses a row where only some are
// NULL. An absent breakdown is written back as NULL rather than as zeroes.
constexpr const char* kUpsertMonthFlow = R"(
    INSERT INTO month_flow (profile_id, month, income_minor, needs_minor, wants_minor,
                            saving_minor, currency, saving_buffer_minor, saving_invest_minor,
                            saving_amort_minor)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT (profile_id, month) DO UPDATE SET
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
    WHERE profile_id = ?
    ORDER BY changed_at
)";

constexpr const char* kInsertPhaseHistory =
    "INSERT INTO phase_history (profile_id, changed_at, phase) VALUES (?, ?, ?)";

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

    /// The row id SQLite assigned to the last insert.
    QVariant lastInsertId() const { return m_query.lastInsertId(); }

    /// Rows matched by the last exec. An upsert whose conflict update was
    /// filtered out by the owner check reports zero, which is how a write that
    /// tried to overwrite another household's row is caught.
    int affected() const { return m_query.numRowsAffected(); }

private:
    QSqlQuery m_query;
    std::string m_what;
    std::string m_error;
};

/// Every scoped repository starts the same way. Zero means no profile has been
/// chosen yet, and running the statement would either return every household's
/// rows or none at all; neither is a truthful answer to "what does this
/// household own".
application::Status requireActiveProfile(std::int64_t profileId, std::string_view what)
{
    if (profileId > 0)
        return application::Status::success();
    return application::Status::err(std::string{what} + ": no active profile");
}

/// Whether a valuation's asset or a debt balance's liability exists and, when it
/// does, whether it belongs to the repository's active household.
///
/// The two are different answers: a row that is simply gone is the ordinary
/// "nothing recorded", while a row owned by another profile is a caller asking
/// about the wrong household and must not be told anything about it.
enum class ParentOwnership { Missing, Owned, OtherProfile };

application::Result<ParentOwnership> parentOwnership(const QSqlDatabase& connection,
                                                     const char* statement,
                                                     std::string_view parentId,
                                                     std::int64_t profileId, std::string_view what)
{
    using Outcome = application::Result<ParentOwnership>;

    Statement query{connection, statement, what};
    const application::Status ran = query.exec({toQString(parentId)});
    if (!ran.ok())
        return Outcome::err(ran.error());
    if (!query.next())
        return Outcome::ok(ParentOwnership::Missing);

    if (query.value(0).toLongLong() == static_cast<qlonglong>(profileId))
        return Outcome::ok(ParentOwnership::Owned);
    return Outcome::ok(ParentOwnership::OtherProfile);
}

/// A read may ask about a holding that is no longer there - the caller is
/// describing a row it is about to delete - but it may not reach into another
/// household's history. Missing is left to the read itself, which returns no
/// rows.
application::Status requireReadableParent(const QSqlDatabase& connection, const char* statement,
                                          std::string_view parentId, std::int64_t profileId,
                                          std::string_view what)
{
    const application::Result<ParentOwnership> ownership =
        parentOwnership(connection, statement, parentId, profileId, what);
    if (!ownership.ok())
        return application::Status::err(ownership.error());
    if (ownership.value() == ParentOwnership::OtherProfile)
        return application::Status::err(std::string{what} + " '" + std::string{parentId} +
                                        "' belongs to another profile");
    return application::Status::success();
}

/// A write may only name a holding the active household owns. A missing parent
/// is refused with its own message rather than left to the foreign key, so the
/// caller learns which id was wrong instead of reading SQLite's constraint text.
application::Status requireWritableParent(const QSqlDatabase& connection, const char* statement,
                                          std::string_view parentId, std::int64_t profileId,
                                          std::string_view what)
{
    const application::Result<ParentOwnership> ownership =
        parentOwnership(connection, statement, parentId, profileId, what);
    if (!ownership.ok())
        return application::Status::err(ownership.error());
    if (ownership.value() == ParentOwnership::Missing)
        return application::Status::err(std::string{what} + " '" + std::string{parentId} +
                                        "' does not exist");
    if (ownership.value() == ParentOwnership::OtherProfile)
        return application::Status::err(std::string{what} + " '" + std::string{parentId} +
                                        "' belongs to another profile");
    return application::Status::success();
}

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

application::Result<domain::Profile> decodeProfileRow(const Statement& row,
                                                      const std::string& context)
{
    using Outcome = application::Result<domain::Profile>;

    domain::Profile profile;
    profile.id = row.value(0).toLongLong();
    profile.displayName = toStd(row.value(1).toString());

    const application::Result<domain::Currency> currency = decodeCurrency(row.value(2), context);
    if (!currency.ok())
        return Outcome::err(currency.error());
    profile.baseCurrency = currency.value();

    const application::Result<domain::Phase> phase = decodePhase(row.value(3), context);
    if (!phase.ok())
        return Outcome::err(phase.error());
    profile.phase = phase.value();

    profile.riskTolerance = domain::Percentage::fromBasisPoints(row.value(4).toInt());

    domain::Jurisdiction jurisdiction;
    jurisdiction.id = toStd(row.value(5).toString());
    jurisdiction.safeWithdrawalRate = domain::Percentage::fromBasisPoints(row.value(6).toInt());
    jurisdiction.capitalTaxDrag = domain::Percentage::fromBasisPoints(row.value(7).toInt());
    jurisdiction.fiMultiplierLow = row.value(8).toInt();
    jurisdiction.fiMultiplierDefault = row.value(9).toInt();
    jurisdiction.fiMultiplierHigh = row.value(10).toInt();
    profile.jurisdiction = jurisdiction;

    const application::Result<domain::TrackingMode> trackingMode =
        decodeTrackingMode(row.value(11), context);
    if (!trackingMode.ok())
        return Outcome::err(trackingMode.error());
    profile.trackingMode = trackingMode.value();

    // A file written before the flag existed reads 0 here, which is what it
    // meant: nothing in it was generated by the application.
    profile.isExample = row.value(12).toInt() != 0;

    return Outcome::ok(std::move(profile));
}

/// The profile the scoped repositories read, as stored in meta. Absent is the
/// ordinary state of a database that has never held a household.
application::Result<std::optional<std::int64_t>> readActiveProfileId(const QSqlDatabase& connection)
{
    using Outcome = application::Result<std::optional<std::int64_t>>;

    Statement statement{connection, kSelectActiveProfileId, "load active profile"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());
    if (!statement.next())
        return Outcome::ok(std::nullopt);

    const QString stored = statement.value(0).toString();
    bool parsed = false;
    const qlonglong id = stored.toLongLong(&parsed);
    if (!parsed || id <= 0)
        return Outcome::err("profile: active profile id '" + toStd(stored) +
                            "' is not a profile id");
    return Outcome::ok(static_cast<std::int64_t>(id));
}

application::Status writeActiveProfileId(const QSqlDatabase& connection, std::int64_t id)
{
    return Statement{connection, kSetActiveProfileId, "set active profile"}.exec(
        {QString::number(id)});
}

application::Result<bool> profileExists(const QSqlDatabase& connection, std::int64_t id)
{
    using Outcome = application::Result<bool>;

    Statement statement{connection, kSelectProfileExists, "find profile"};
    const application::Status ran = statement.exec({QString::number(id)});
    if (!ran.ok())
        return Outcome::err(ran.error());
    return Outcome::ok(statement.next());
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

    const application::Result<std::optional<std::int64_t>> active =
        readActiveProfileId(m_connection);
    if (!active.ok())
        return Outcome::err(active.error());
    if (!active.value())
        return Outcome::ok(std::nullopt);

    Statement statement{m_connection, kSelectProfile, "load profile"};
    const application::Status ran = statement.exec({QString::number(*active.value())});
    if (!ran.ok())
        return Outcome::err(ran.error());

    // A pointer into a missing row is not "no profile": the file says which
    // household is active and the row it names is gone, which is corrupt data
    // the caller has to see rather than a fresh empty state.
    if (!statement.next())
        return Outcome::err("profile: active profile " + std::to_string(*active.value()) +
                            " does not exist");

    application::Result<domain::Profile> decoded = decodeProfileRow(statement, "profile");
    if (!decoded.ok())
        return Outcome::err(decoded.error());
    return Outcome::ok(std::move(decoded.value()));
}

application::Result<std::optional<domain::Profile>>
SqliteProfileRepository::loadById(std::int64_t id) const
{
    using Outcome = application::Result<std::optional<domain::Profile>>;

    if (id <= 0)
        return Outcome::ok(std::nullopt);

    Statement statement{m_connection, kSelectProfile, "load profile"};
    const application::Status ran = statement.exec({QString::number(id)});
    if (!ran.ok())
        return Outcome::err(ran.error());
    if (!statement.next())
        return Outcome::ok(std::nullopt);

    application::Result<domain::Profile> decoded = decodeProfileRow(statement, "profile");
    if (!decoded.ok())
        return Outcome::err(decoded.error());
    return Outcome::ok(std::move(decoded.value()));
}

application::Result<std::vector<application::ProfileSummary>> SqliteProfileRepository::list() const
{
    using Outcome = application::Result<std::vector<application::ProfileSummary>>;

    Statement statement{m_connection, kSelectProfiles, "list profiles"};
    const application::Status ran = statement.exec();
    if (!ran.ok())
        return Outcome::err(ran.error());

    std::vector<application::ProfileSummary> profiles;
    while (statement.next()) {
        application::ProfileSummary summary;
        summary.id = statement.value(0).toLongLong();
        summary.displayName = toStd(statement.value(1).toString());
        summary.baseCurrency = toStd(statement.value(2).toString());
        summary.trackingMode = toStd(statement.value(3).toString());

        const application::Result<domain::Phase> phase =
            decodePhase(statement.value(4), "profile '" + summary.displayName + "'");
        if (!phase.ok())
            return Outcome::err(phase.error());
        summary.phase = phase.value();
        summary.isExample = statement.value(5).toInt() != 0;
        profiles.push_back(std::move(summary));
    }
    return Outcome::ok(std::move(profiles));
}

application::Status SqliteProfileRepository::save(const domain::Profile& profile)
{
    const auto values = [&profile]() {
        return QVariantList{
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
            profile.isExample ? 1 : 0,
        };
    };

    const auto update = [this, &values](std::int64_t id) -> application::Status {
        QVariantList bound = values();
        bound.push_back(QString::number(id));
        Statement statement{m_connection, kUpdateProfile, "save profile"};
        const application::Status ran = statement.exec(bound);
        if (!ran.ok())
            return ran;
        if (statement.affected() == 0)
            return application::Status::err("profile " + std::to_string(id) + " does not exist");
        return application::Status::success();
    };

    if (profile.id > 0)
        return update(profile.id);

    // A profile without an id is either the first household this file has ever
    // held or an edit that lost track of which row it came from. The first is
    // the wizard's ordinary path; the second edits the active household rather
    // than silently becoming a second one.
    const application::Result<std::optional<std::int64_t>> active =
        readActiveProfileId(m_connection);
    if (!active.ok())
        return application::Status::err(active.error());
    if (active.value())
        return update(*active.value());

    Statement statement{m_connection, kInsertProfile, "save profile"};
    const application::Status inserted = statement.exec({
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
        profile.isExample ? 1 : 0,
        toQString(nowIso()),
    });
    if (!inserted.ok())
        return inserted;

    const qlonglong id = statement.lastInsertId().toLongLong();
    if (id <= 0)
        return application::Status::err("save profile: the new profile got no id");
    return writeActiveProfileId(m_connection, id);
}

application::Result<domain::Profile> SqliteProfileRepository::create(const domain::Profile& profile)
{
    using Outcome = application::Result<domain::Profile>;

    Statement statement{m_connection, kInsertProfile, "create profile"};
    const application::Status inserted = statement.exec({
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
        profile.isExample ? 1 : 0,
        toQString(nowIso()),
    });
    if (!inserted.ok())
        return Outcome::err(inserted.error());

    const qlonglong id = statement.lastInsertId().toLongLong();
    if (id <= 0)
        return Outcome::err("create profile: the new profile got no id");

    const application::Status activated =
        writeActiveProfileId(m_connection, static_cast<std::int64_t>(id));
    if (!activated.ok())
        return Outcome::err(activated.error());

    domain::Profile created = profile;
    created.id = static_cast<std::int64_t>(id);
    return Outcome::ok(std::move(created));
}

application::Status SqliteProfileRepository::remove(std::int64_t id)
{
    // The count, the delete and the pointer are one decision. If the delete
    // committed before the pointer was rewritten, a failure in between would
    // leave meta naming a profile that is gone, and the application could not
    // open the file at all.
    if (!m_connection.transaction())
        return application::Status::err("remove profile: cannot begin transaction: " +
                                        toStd(m_connection.lastError().text()));

    const auto fail = [this](const std::string& message) {
        m_connection.rollback();
        return application::Status::err(message);
    };

    Statement count{m_connection, kSelectProfileCount, "count profiles"};
    const application::Status counted = count.exec();
    if (!counted.ok())
        return fail(counted.error());
    if (!count.next())
        return fail("count profiles: no result");
    if (count.value(0).toInt() <= 1)
        return fail("remove profile: cannot remove the last profile");

    // The data goes with it: asset, liability, planned_expense, month_flow and
    // phase_history all cascade from profile, and valuation and debt_balance
    // cascade from the asset and liability rows that do.
    const application::Status removed =
        Statement{m_connection, kDeleteProfile, "remove profile"}.exec({QString::number(id)});
    if (!removed.ok())
        return fail(removed.error());

    // Removing the household that was being read leaves the file pointing at a
    // row that is gone. The next household by id takes over, so the application
    // always has somewhere to write.
    const application::Result<std::optional<std::int64_t>> active =
        readActiveProfileId(m_connection);
    if (!active.ok())
        return fail(active.error());
    if (active.value() && *active.value() == id) {
        Statement first{m_connection, kSelectFirstProfileId, "find first profile"};
        const application::Status ran = first.exec();
        if (!ran.ok())
            return fail(ran.error());
        if (first.next()) {
            const application::Status pointed =
                writeActiveProfileId(m_connection, first.value(0).toLongLong());
            if (!pointed.ok())
                return fail(pointed.error());
        }
    }

    if (!m_connection.commit())
        return fail("remove profile: cannot commit: " + toStd(m_connection.lastError().text()));
    return application::Status::success();
}

application::Status SqliteProfileRepository::setActive(std::int64_t id)
{
    const application::Result<bool> exists = profileExists(m_connection, id);
    if (!exists.ok())
        return application::Status::err(exists.error());
    if (!exists.value())
        return application::Status::err("profile " + std::to_string(id) + " does not exist");
    return writeActiveProfileId(m_connection, id);
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

    const application::Status scoped = requireActiveProfile(m_activeProfileId, "load assets");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectAssets, "load assets"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "save asset");
    if (!scoped.ok())
        return scoped;

    Statement statement{m_connection, kUpsertAsset, "save asset"};
    const application::Status ran = statement.exec({
        toQString(asset.id),
        toQString(asset.name),
        QString::fromUtf8(domain::storageKey(asset.kind)),
        QString::fromUtf8(domain::storageKey(asset.bucket)),
        asset.liquid ? 1 : 0,
        asset.includedInHeadline ? 1 : 0,
        QString::number(m_activeProfileId),
    });
    if (!ran.ok())
        return ran;
    // The id is globally unique, so a conflict with another household's row is
    // a caller that reused an id rather than an edit. The owner check in the
    // upsert leaves that row alone; refusing the write says so.
    if (statement.affected() == 0)
        return application::Status::err("save asset: asset '" + asset.id +
                                        "' belongs to another profile");
    return application::Status::success();
}

application::Status SqliteAssetRepository::remove(std::string_view id)
{
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "delete asset");
    if (!scoped.ok())
        return scoped;

    return Statement{m_connection, kDeleteAsset, "delete asset"}.exec(
        {toQString(id), QString::number(m_activeProfileId)});
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

    const application::Status scoped = requireActiveProfile(m_activeProfileId, "load liabilities");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectLiabilities, "load liabilities"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "save liability");
    if (!scoped.ok())
        return scoped;

    Statement statement{m_connection, kUpsertLiability, "save liability"};
    const application::Status ran = statement.exec({
        toQString(liability.id),
        toQString(liability.name),
        QString::fromUtf8(domain::storageKey(liability.kind)),
        liability.interestRate.partsPer100k(),
        QString::fromUtf8(domain::storageKey(liability.style)),
        static_cast<qlonglong>(liability.regularPayment.minorUnits()),
        static_cast<qlonglong>(liability.monthlyFee.minorUnits()),
        liability.termMonths,
        QString::number(m_activeProfileId),
    });
    if (!ran.ok())
        return ran;
    if (statement.affected() == 0)
        return application::Status::err("save liability: liability '" + liability.id +
                                        "' belongs to another profile");
    return application::Status::success();
}

application::Status SqliteLiabilityRepository::remove(std::string_view id)
{
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "delete liability");
    if (!scoped.ok())
        return scoped;

    return Statement{m_connection, kDeleteLiability, "delete liability"}.exec(
        {toQString(id), QString::number(m_activeProfileId)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load planned expenses");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectPlannedExpenses, "load planned expenses"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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
    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "save planned expense");
    if (!scoped.ok())
        return scoped;

    Statement statement{m_connection, kUpsertPlannedExpense, "save planned expense"};
    const application::Status ran = statement.exec({
        toQString(expense.id),
        toQString(expense.name),
        static_cast<qint64>(expense.amount.minorUnits()),
        monthText(expense.dueMonth),
        QString::fromUtf8(domain::storageKey(expense.amount.currency())),
        QString::number(m_activeProfileId),
    });
    if (!ran.ok())
        return ran;
    if (statement.affected() == 0)
        return application::Status::err("save planned expense: planned expense '" + expense.id +
                                        "' belongs to another profile");
    return application::Status::success();
}

application::Status SqlitePlannedExpenseRepository::remove(std::string_view id)
{
    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "delete planned expense");
    if (!scoped.ok())
        return scoped;

    return Statement{m_connection, kDeletePlannedExpense, "delete planned expense"}.exec(
        {toQString(id), QString::number(m_activeProfileId)});
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

    const application::Status scoped = requireActiveProfile(m_activeProfileId, "load valuation");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable = requireReadableParent(
        m_connection, kSelectAssetOwner, assetId, m_activeProfileId, "load valuation: asset");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectValuation, "load valuation"};
    const application::Status ran =
        statement.exec({toQString(assetId), monthText(month), QString::number(m_activeProfileId)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load latest valuation");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable =
        requireReadableParent(m_connection, kSelectAssetOwner, assetId, m_activeProfileId,
                              "load latest valuation: asset");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectLatestValuation, "load latest valuation"};
    const application::Status ran =
        statement.exec({toQString(assetId), QString::number(m_activeProfileId)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load valuation history");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable =
        requireReadableParent(m_connection, kSelectAssetOwner, assetId, m_activeProfileId,
                              "load valuation history: asset");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectValuationHistory, "load valuation history"};
    const application::Status ran =
        statement.exec({toQString(assetId), QString::number(m_activeProfileId)});
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
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "save valuation");
    if (!scoped.ok())
        return scoped;

    const application::Status writable = requireWritableParent(
        m_connection, kSelectAssetOwner, assetId, m_activeProfileId, "save valuation: asset");
    if (!writable.ok())
        return writable;

    Statement statement{m_connection, kUpsertValuation, "save valuation"};
    const application::Status ran = statement.exec({
        toQString(assetId),
        monthText(month),
        static_cast<qint64>(balance.minorUnits()),
        QString::fromUtf8(domain::storageKey(balance.currency())),
        toQString(assetId),
        QString::number(m_activeProfileId),
    });
    if (!ran.ok())
        return ran;
    // The ownership check above already refused a foreign asset; this catches
    // the same disagreement if it happened between the check and the write.
    if (statement.affected() == 0)
        return application::Status::err("save valuation: asset '" + std::string{assetId} +
                                        "' does not belong to the active profile");
    return application::Status::success();
}

application::Result<std::optional<domain::YearMonth>>
SqliteValuationRepository::mostRecentMonth() const
{
    using Outcome = application::Result<std::optional<domain::YearMonth>>;

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load latest valuation month");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectLatestValuationMonth, "load latest valuation month"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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

    const application::Status scoped = requireActiveProfile(m_activeProfileId, "load debt balance");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable =
        requireReadableParent(m_connection, kSelectLiabilityOwner, liabilityId, m_activeProfileId,
                              "load debt balance: liability");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectDebtBalance, "load debt balance"};
    const application::Status ran = statement.exec(
        {toQString(liabilityId), monthText(month), QString::number(m_activeProfileId)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load latest debt balance");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable =
        requireReadableParent(m_connection, kSelectLiabilityOwner, liabilityId, m_activeProfileId,
                              "load latest debt balance: liability");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectLatestDebtBalance, "load latest debt balance"};
    const application::Status ran =
        statement.exec({toQString(liabilityId), QString::number(m_activeProfileId)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load debt balance history");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    const application::Status readable =
        requireReadableParent(m_connection, kSelectLiabilityOwner, liabilityId, m_activeProfileId,
                              "load debt balance history: liability");
    if (!readable.ok())
        return Outcome::err(readable.error());

    Statement statement{m_connection, kSelectDebtBalanceHistory, "load debt balance history"};
    const application::Status ran =
        statement.exec({toQString(liabilityId), QString::number(m_activeProfileId)});
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
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "save debt balance");
    if (!scoped.ok())
        return scoped;

    const application::Status writable =
        requireWritableParent(m_connection, kSelectLiabilityOwner, liabilityId, m_activeProfileId,
                              "save debt balance: liability");
    if (!writable.ok())
        return writable;

    Statement statement{m_connection, kUpsertDebtBalance, "save debt balance"};
    const application::Status ran = statement.exec({
        toQString(liabilityId),
        monthText(month),
        static_cast<qint64>(balance.minorUnits()),
        QString::fromUtf8(domain::storageKey(balance.currency())),
        toQString(liabilityId),
        QString::number(m_activeProfileId),
    });
    if (!ran.ok())
        return ran;
    if (statement.affected() == 0)
        return application::Status::err("save debt balance: liability '" +
                                        std::string{liabilityId} +
                                        "' does not belong to the active profile");
    return application::Status::success();
}

application::Result<std::optional<domain::YearMonth>>
SqliteDebtBalanceRepository::mostRecentMonth() const
{
    using Outcome = application::Result<std::optional<domain::YearMonth>>;

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load latest debt balance month");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectLatestDebtBalanceMonth,
                        "load latest debt balance month"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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

    const application::Status scoped = requireActiveProfile(m_activeProfileId, "load month flow");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectMonthFlow, "load month flow"};
    const application::Status ran =
        statement.exec({QString::number(m_activeProfileId), monthText(month)});
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load recent month flows");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectRecentMonthFlows, "load recent month flows"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId), count});
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
    const application::Status scoped = requireActiveProfile(m_activeProfileId, "save month flow");
    if (!scoped.ok())
        return scoped;

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
        QString::number(m_activeProfileId),
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

    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "load phase history");
    if (!scoped.ok())
        return Outcome::err(scoped.error());

    Statement statement{m_connection, kSelectPhaseHistory, "load phase history"};
    const application::Status ran = statement.exec({QString::number(m_activeProfileId)});
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
    const application::Status scoped =
        requireActiveProfile(m_activeProfileId, "append phase change");
    if (!scoped.ok())
        return scoped;

    return Statement{m_connection, kInsertPhaseHistory, "append phase change"}.exec(
        {QString::number(m_activeProfileId), toQString(change.changedAt), phaseText(change.phase)});
}

} // namespace hinkwise::infrastructure::sqlite
