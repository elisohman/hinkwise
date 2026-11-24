// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backup.h"

#include "domain/serialisation.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hinkwise::infrastructure::json {

namespace {

std::string toStd(const QString& text)
{
    return text.toStdString();
}

/// Keys are written as QString because QJsonObject offers overloads for
/// QString, QStringView and QLatin1StringView, and a bare literal would make the
/// call ambiguous. One helper keeps that detail in one place.
QString key(const char* field)
{
    return QString::fromUtf8(field);
}

void put(QJsonObject& object, const char* field, const QJsonValue& value)
{
    object.insert(key(field), value);
}

QString storageKeyText(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string indexed(const char* field, qsizetype index)
{
    return "backup: '" + std::string{field} + "'[" + std::to_string(index) + "]";
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

QJsonObject moneyToJson(const domain::Money& money)
{
    QJsonObject object;
    put(object, "minor", static_cast<qint64>(money.minorUnits()));
    put(object, "currency", storageKeyText(domain::storageKey(money.currency())));
    return object;
}

QJsonObject jurisdictionToJson(const domain::Jurisdiction& jurisdiction)
{
    QJsonObject object;
    put(object, "id", storageKeyText(jurisdiction.id));
    put(object, "safeWithdrawalBp", jurisdiction.safeWithdrawalRate.basisPoints());
    put(object, "capitalTaxBp", jurisdiction.capitalTaxDrag.basisPoints());
    put(object, "fiMultiplierLow", jurisdiction.fiMultiplierLow);
    put(object, "fiMultiplierDefault", jurisdiction.fiMultiplierDefault);
    put(object, "fiMultiplierHigh", jurisdiction.fiMultiplierHigh);
    return object;
}

QJsonObject profileToJson(const domain::Profile& profile)
{
    QJsonObject object;
    put(object, "displayName", storageKeyText(profile.displayName));
    put(object, "baseCurrency", storageKeyText(domain::storageKey(profile.baseCurrency)));
    put(object, "phase", storageKeyText(domain::storageKey(profile.phase)));
    put(object, "riskToleranceBp", profile.riskTolerance.basisPoints());
    put(object, "trackingMode", storageKeyText(domain::storageKey(profile.trackingMode)));
    put(object, "jurisdiction", jurisdictionToJson(profile.jurisdiction));
    return object;
}

QJsonObject assetToJson(const domain::Asset& asset)
{
    QJsonObject object;
    put(object, "id", storageKeyText(asset.id));
    put(object, "name", storageKeyText(asset.name));
    put(object, "kind", storageKeyText(domain::storageKey(asset.kind)));
    put(object, "bucket", storageKeyText(domain::storageKey(asset.bucket)));
    put(object, "liquid", asset.liquid);
    put(object, "includedInHeadline", asset.includedInHeadline);
    return object;
}

QJsonObject liabilityToJson(const domain::Liability& liability)
{
    QJsonObject object;
    put(object, "id", storageKeyText(liability.id));
    put(object, "name", storageKeyText(liability.name));
    put(object, "kind", storageKeyText(domain::storageKey(liability.kind)));
    put(object, "interestBp", liability.interestRate.basisPoints());
    return object;
}

QJsonObject plannedExpenseToJson(const domain::PlannedExpense& expense)
{
    QJsonObject object;
    put(object, "id", storageKeyText(expense.id));
    put(object, "name", storageKeyText(expense.name));
    put(object, "amount", moneyToJson(expense.amount));
    put(object, "dueMonth", storageKeyText(expense.dueMonth.toString()));
    return object;
}

QJsonArray pointsToJson(const std::vector<MoneyPoint>& points)
{
    QJsonArray array;
    for (const MoneyPoint& point : points) {
        QJsonObject object;
        put(object, "month", storageKeyText(point.month.toString()));
        put(object, "amount", moneyToJson(point.amount));
        array.append(object);
    }
    return array;
}

QJsonObject savingsBreakdownToJson(const domain::SavingsBreakdown& split)
{
    QJsonObject object;
    put(object, "buffer", moneyToJson(split.toBuffer));
    put(object, "investing", moneyToJson(split.toInvesting));
    put(object, "amortisation", moneyToJson(split.toAmortisation));
    return object;
}

QJsonObject monthFlowToJson(const application::MonthFlowRecord& record)
{
    QJsonObject object;
    put(object, "month", storageKeyText(record.month.toString()));
    put(object, "income", moneyToJson(record.flow.income));
    put(object, "needs", moneyToJson(record.flow.needs));
    put(object, "wants", moneyToJson(record.flow.wants));
    put(object, "saving", moneyToJson(record.flow.saving));
    // A month with no recorded breakdown has no key at all, which is how an
    // older file reads too: absence says "the total is all we know".
    if (record.flow.savingSplit)
        put(object, "savingSplit", savingsBreakdownToJson(*record.flow.savingSplit));
    return object;
}

QJsonObject phaseChangeToJson(const application::PhaseChange& change)
{
    QJsonObject object;
    put(object, "changedAt", storageKeyText(change.changedAt));
    put(object, "phase", storageKeyText(domain::storageKey(change.phase)));
    return object;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

template <typename T> application::Result<T> missing(const std::string& context, const char* field)
{
    return application::Result<T>::err(context + ": no '" + field + "'");
}

application::Result<QJsonObject> asObject(const QJsonValue& value, const std::string& context)
{
    if (value.isUndefined())
        return application::Result<QJsonObject>::err(context + ": missing");
    if (!value.isObject())
        return application::Result<QJsonObject>::err(context + ": not a JSON object");
    return application::Result<QJsonObject>::ok(value.toObject());
}

application::Result<QJsonArray> asArray(const QJsonValue& value, const std::string& context)
{
    if (value.isUndefined())
        return application::Result<QJsonArray>::err(context + ": missing");
    if (!value.isArray())
        return application::Result<QJsonArray>::err(context + ": not a JSON array");
    return application::Result<QJsonArray>::ok(value.toArray());
}

application::Result<QString> requireString(const QJsonObject& object, const char* field,
                                           const std::string& context)
{
    const QJsonValue value = object.value(key(field));
    if (value.isUndefined())
        return missing<QString>(context, field);
    if (!value.isString())
        return application::Result<QString>::err(context + ": '" + field + "' is not a string");
    return application::Result<QString>::ok(value.toString());
}

application::Result<std::int64_t> requireInteger(const QJsonObject& object, const char* field,
                                                 const std::string& context)
{
    const QJsonValue value = object.value(key(field));
    if (value.isUndefined())
        return missing<std::int64_t>(context, field);
    if (!value.isDouble())
        return application::Result<std::int64_t>::err(context + ": '" + field +
                                                      "' is not a number");
    return application::Result<std::int64_t>::ok(static_cast<std::int64_t>(value.toInteger()));
}

application::Result<int> requireInt(const QJsonObject& object, const char* field,
                                    const std::string& context)
{
    const application::Result<std::int64_t> value = requireInteger(object, field, context);
    if (!value.ok())
        return application::Result<int>::err(value.error());
    return application::Result<int>::ok(static_cast<int>(value.value()));
}

application::Result<bool> requireBool(const QJsonObject& object, const char* field,
                                      const std::string& context)
{
    const QJsonValue value = object.value(key(field));
    if (value.isUndefined())
        return missing<bool>(context, field);
    if (!value.isBool())
        return application::Result<bool>::err(context + ": '" + field + "' is not a boolean");
    return application::Result<bool>::ok(value.toBool());
}

template <typename Enum>
application::Result<Enum> requireEnum(const QJsonObject& object, const char* field,
                                      const std::string& context,
                                      std::optional<Enum> (*fromStorageKey)(std::string_view))
{
    const application::Result<QString> text = requireString(object, field, context);
    if (!text.ok())
        return application::Result<Enum>::err(text.error());

    const std::string stored = toStd(text.value());
    const std::optional<Enum> decoded = fromStorageKey(stored);
    if (!decoded)
        return application::Result<Enum>::err(context + ": unknown " + field + " '" + stored + "'");
    return application::Result<Enum>::ok(*decoded);
}

application::Result<domain::YearMonth> requireMonth(const QJsonObject& object, const char* field,
                                                    const std::string& context)
{
    const application::Result<QString> text = requireString(object, field, context);
    if (!text.ok())
        return application::Result<domain::YearMonth>::err(text.error());

    const std::string stored = toStd(text.value());
    const std::optional<domain::YearMonth> month = domain::YearMonth::parse(stored);
    if (!month)
        return application::Result<domain::YearMonth>::err(context + ": bad " + field + " '" +
                                                           stored + "' (expected YYYY-MM)");
    return application::Result<domain::YearMonth>::ok(*month);
}

application::Result<domain::Money> requireMoney(const QJsonObject& object, const char* field,
                                                const std::string& context)
{
    const application::Result<QJsonObject> money = asObject(object.value(key(field)), context);
    if (!money.ok())
        return application::Result<domain::Money>::err(money.error());

    const std::string where = context + ": '" + field + "'";
    const application::Result<std::int64_t> minor = requireInteger(money.value(), "minor", where);
    if (!minor.ok())
        return application::Result<domain::Money>::err(minor.error());

    const application::Result<domain::Currency> currency = requireEnum<domain::Currency>(
        money.value(), "currency", where, domain::currencyFromStorageKey);
    if (!currency.ok())
        return application::Result<domain::Money>::err(currency.error());

    return application::Result<domain::Money>::ok(domain::Money{minor.value(), currency.value()});
}

application::Result<domain::Jurisdiction> jurisdictionFromJson(const QJsonValue& value,
                                                               const std::string& context)
{
    using Outcome = application::Result<domain::Jurisdiction>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    domain::Jurisdiction jurisdiction;
    const application::Result<QString> id = requireString(object.value(), "id", context);
    if (!id.ok())
        return Outcome::err(id.error());
    jurisdiction.id = toStd(id.value());

    const application::Result<int> safeWithdrawal =
        requireInt(object.value(), "safeWithdrawalBp", context);
    if (!safeWithdrawal.ok())
        return Outcome::err(safeWithdrawal.error());
    jurisdiction.safeWithdrawalRate = domain::Percentage::fromBasisPoints(safeWithdrawal.value());

    const application::Result<int> capitalTax = requireInt(object.value(), "capitalTaxBp", context);
    if (!capitalTax.ok())
        return Outcome::err(capitalTax.error());
    jurisdiction.capitalTaxDrag = domain::Percentage::fromBasisPoints(capitalTax.value());

    const application::Result<int> low = requireInt(object.value(), "fiMultiplierLow", context);
    if (!low.ok())
        return Outcome::err(low.error());
    jurisdiction.fiMultiplierLow = low.value();

    const application::Result<int> middle =
        requireInt(object.value(), "fiMultiplierDefault", context);
    if (!middle.ok())
        return Outcome::err(middle.error());
    jurisdiction.fiMultiplierDefault = middle.value();

    const application::Result<int> high = requireInt(object.value(), "fiMultiplierHigh", context);
    if (!high.ok())
        return Outcome::err(high.error());
    jurisdiction.fiMultiplierHigh = high.value();

    return Outcome::ok(jurisdiction);
}

application::Result<domain::Profile> profileFromJson(const QJsonValue& value,
                                                     const std::string& context)
{
    using Outcome = application::Result<domain::Profile>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    domain::Profile profile;
    const application::Result<QString> name = requireString(object.value(), "displayName", context);
    if (!name.ok())
        return Outcome::err(name.error());
    profile.displayName = toStd(name.value());

    const application::Result<domain::Currency> currency = requireEnum<domain::Currency>(
        object.value(), "baseCurrency", context, domain::currencyFromStorageKey);
    if (!currency.ok())
        return Outcome::err(currency.error());
    profile.baseCurrency = currency.value();

    const application::Result<domain::Phase> phase =
        requireEnum<domain::Phase>(object.value(), "phase", context, domain::phaseFromStorageKey);
    if (!phase.ok())
        return Outcome::err(phase.error());
    profile.phase = phase.value();

    const application::Result<int> riskTolerance =
        requireInt(object.value(), "riskToleranceBp", context);
    if (!riskTolerance.ok())
        return Outcome::err(riskTolerance.error());
    profile.riskTolerance = domain::Percentage::fromBasisPoints(riskTolerance.value());

    // The tracking mode arrived after the first backup format, so a file
    // written before it has no 'trackingMode' key at all: absence means
    // Planning, the mode an existing household was already working in. Text
    // that is there and unrecognised is a different matter - the key claims to
    // say something, and guessing what would be inventing a setting.
    const QJsonValue trackingMode = object.value().value(key("trackingMode"));
    if (trackingMode.isUndefined()) {
        profile.trackingMode = domain::TrackingMode::Planning;
    } else {
        const application::Result<domain::TrackingMode> decoded = requireEnum<domain::TrackingMode>(
            object.value(), "trackingMode", context, domain::trackingModeFromStorageKey);
        if (!decoded.ok())
            return Outcome::err(decoded.error());
        profile.trackingMode = decoded.value();
    }

    const application::Result<domain::Jurisdiction> jurisdiction =
        jurisdictionFromJson(object.value().value(key("jurisdiction")), context + ": jurisdiction");
    if (!jurisdiction.ok())
        return Outcome::err(jurisdiction.error());
    profile.jurisdiction = jurisdiction.value();

    return Outcome::ok(profile);
}

application::Result<domain::Asset> assetFromJson(const QJsonValue& value,
                                                 const std::string& context)
{
    using Outcome = application::Result<domain::Asset>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    domain::Asset asset;
    const application::Result<QString> id = requireString(object.value(), "id", context);
    if (!id.ok())
        return Outcome::err(id.error());
    asset.id = toStd(id.value());

    const application::Result<QString> name = requireString(object.value(), "name", context);
    if (!name.ok())
        return Outcome::err(name.error());
    asset.name = toStd(name.value());

    const application::Result<domain::AssetKind> kind = requireEnum<domain::AssetKind>(
        object.value(), "kind", context, domain::assetKindFromStorageKey);
    if (!kind.ok())
        return Outcome::err(kind.error());
    asset.kind = kind.value();

    const application::Result<domain::Bucket> bucket = requireEnum<domain::Bucket>(
        object.value(), "bucket", context, domain::bucketFromStorageKey);
    if (!bucket.ok())
        return Outcome::err(bucket.error());
    asset.bucket = bucket.value();

    const application::Result<bool> liquid = requireBool(object.value(), "liquid", context);
    if (!liquid.ok())
        return Outcome::err(liquid.error());
    asset.liquid = liquid.value();

    const application::Result<bool> included =
        requireBool(object.value(), "includedInHeadline", context);
    if (!included.ok())
        return Outcome::err(included.error());
    asset.includedInHeadline = included.value();

    return Outcome::ok(asset);
}

application::Result<domain::Liability> liabilityFromJson(const QJsonValue& value,
                                                         const std::string& context)
{
    using Outcome = application::Result<domain::Liability>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    domain::Liability liability;
    const application::Result<QString> id = requireString(object.value(), "id", context);
    if (!id.ok())
        return Outcome::err(id.error());
    liability.id = toStd(id.value());

    const application::Result<QString> name = requireString(object.value(), "name", context);
    if (!name.ok())
        return Outcome::err(name.error());
    liability.name = toStd(name.value());

    const application::Result<domain::LiabilityKind> kind = requireEnum<domain::LiabilityKind>(
        object.value(), "kind", context, domain::liabilityKindFromStorageKey);
    if (!kind.ok())
        return Outcome::err(kind.error());
    liability.kind = kind.value();

    const application::Result<int> interest = requireInt(object.value(), "interestBp", context);
    if (!interest.ok())
        return Outcome::err(interest.error());
    liability.interestRate = domain::Percentage::fromBasisPoints(interest.value());

    return Outcome::ok(liability);
}

application::Result<domain::PlannedExpense> plannedExpenseFromJson(const QJsonValue& value,
                                                                   const std::string& context)
{
    using Outcome = application::Result<domain::PlannedExpense>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    domain::PlannedExpense expense;
    const application::Result<QString> id = requireString(object.value(), "id", context);
    if (!id.ok())
        return Outcome::err(id.error());
    expense.id = toStd(id.value());

    const application::Result<QString> name = requireString(object.value(), "name", context);
    if (!name.ok())
        return Outcome::err(name.error());
    expense.name = toStd(name.value());

    const application::Result<domain::Money> amount =
        requireMoney(object.value(), "amount", context);
    if (!amount.ok())
        return Outcome::err(amount.error());
    expense.amount = amount.value();

    const application::Result<domain::YearMonth> dueMonth =
        requireMonth(object.value(), "dueMonth", context);
    if (!dueMonth.ok())
        return Outcome::err(dueMonth.error());
    expense.dueMonth = dueMonth.value();

    return Outcome::ok(expense);
}

application::Result<std::vector<MoneyPoint>> pointsFromJson(const QJsonValue& value,
                                                            const std::string& context)
{
    using Outcome = application::Result<std::vector<MoneyPoint>>;

    const application::Result<QJsonArray> array = asArray(value, context);
    if (!array.ok())
        return Outcome::err(array.error());

    std::vector<MoneyPoint> points;
    for (qsizetype i = 0; i < array.value().size(); ++i) {
        const std::string where = indexed("points", i);
        const application::Result<QJsonObject> object = asObject(array.value().at(i), where);
        if (!object.ok())
            return Outcome::err(object.error());

        const application::Result<domain::YearMonth> month =
            requireMonth(object.value(), "month", where);
        if (!month.ok())
            return Outcome::err(month.error());

        const application::Result<domain::Money> amount =
            requireMoney(object.value(), "amount", where);
        if (!amount.ok())
            return Outcome::err(amount.error());

        points.push_back(MoneyPoint{month.value(), amount.value()});
    }
    return Outcome::ok(std::move(points));
}

application::Result<application::MonthFlowRecord> monthFlowFromJson(const QJsonValue& value,
                                                                    const std::string& context)
{
    using Outcome = application::Result<application::MonthFlowRecord>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    const application::Result<domain::YearMonth> month =
        requireMonth(object.value(), "month", context);
    if (!month.ok())
        return Outcome::err(month.error());

    domain::MonthFlow flow;
    const application::Result<domain::Money> income =
        requireMoney(object.value(), "income", context);
    if (!income.ok())
        return Outcome::err(income.error());
    flow.income = income.value();

    const application::Result<domain::Money> needs = requireMoney(object.value(), "needs", context);
    if (!needs.ok())
        return Outcome::err(needs.error());
    flow.needs = needs.value();

    const application::Result<domain::Money> wants = requireMoney(object.value(), "wants", context);
    if (!wants.ok())
        return Outcome::err(wants.error());
    flow.wants = wants.value();

    const application::Result<domain::Money> saving =
        requireMoney(object.value(), "saving", context);
    if (!saving.ok())
        return Outcome::err(saving.error());
    flow.saving = saving.value();

    // The breakdown is optional: a file written before it existed, or a month
    // the household only knows the total of, has no 'savingSplit' key.
    const QJsonValue split = object.value().value(key("savingSplit"));
    if (!split.isUndefined()) {
        const std::string where = "backup: month flow " + month.value().toString();
        const application::Result<QJsonObject> splitObject =
            asObject(split, where + ": 'savingSplit'");
        if (!splitObject.ok())
            return Outcome::err(splitObject.error());

        const application::Result<domain::Money> buffer =
            requireMoney(splitObject.value(), "buffer", where + ": 'savingSplit'");
        if (!buffer.ok())
            return Outcome::err(buffer.error());

        const application::Result<domain::Money> investing =
            requireMoney(splitObject.value(), "investing", where + ": 'savingSplit'");
        if (!investing.ok())
            return Outcome::err(investing.error());

        const application::Result<domain::Money> amortisation =
            requireMoney(splitObject.value(), "amortisation", where + ": 'savingSplit'");
        if (!amortisation.ok())
            return Outcome::err(amortisation.error());

        flow.savingSplit =
            domain::SavingsBreakdown{buffer.value(), investing.value(), amortisation.value()};

        // A breakdown that disagrees with its own total is a corrupt file: the
        // parts were edited, or the total was, and guessing which one is right
        // would invent a figure the household never wrote.
        if (!domain::savingSplitMatchesTotal(flow))
            return Outcome::err(where + ": the saving breakdown does not add up to its total " +
                                flow.saving.toString() + " (all three parts must be in " +
                                domain::storageKey(flow.saving.currency()) + ")");
    }

    return Outcome::ok(application::MonthFlowRecord{month.value(), flow});
}

application::Result<application::PhaseChange> phaseChangeFromJson(const QJsonValue& value,
                                                                  const std::string& context)
{
    using Outcome = application::Result<application::PhaseChange>;

    const application::Result<QJsonObject> object = asObject(value, context);
    if (!object.ok())
        return Outcome::err(object.error());

    application::PhaseChange change;
    const application::Result<QString> changedAt =
        requireString(object.value(), "changedAt", context);
    if (!changedAt.ok())
        return Outcome::err(changedAt.error());
    change.changedAt = toStd(changedAt.value());

    const application::Result<domain::Phase> phase =
        requireEnum<domain::Phase>(object.value(), "phase", context, domain::phaseFromStorageKey);
    if (!phase.ok())
        return Outcome::err(phase.error());
    change.phase = phase.value();

    return Outcome::ok(change);
}

} // namespace

std::string toJson(const BackupData& data)
{
    QJsonObject root;
    put(root, "version", kBackupVersion);
    put(root, "profile", profileToJson(data.profile));

    QJsonArray assets;
    for (const domain::Asset& asset : data.assets)
        assets.append(assetToJson(asset));
    put(root, "assets", assets);

    QJsonArray liabilities;
    for (const domain::Liability& liability : data.liabilities)
        liabilities.append(liabilityToJson(liability));
    put(root, "liabilities", liabilities);

    QJsonArray plannedExpenses;
    for (const domain::PlannedExpense& expense : data.plannedExpenses)
        plannedExpenses.append(plannedExpenseToJson(expense));
    put(root, "plannedExpenses", plannedExpenses);

    QJsonArray assetValuations;
    for (const AssetValuations& valuations : data.assetValuations) {
        QJsonObject object;
        put(object, "assetId", storageKeyText(valuations.assetId));
        put(object, "points", pointsToJson(valuations.points));
        assetValuations.append(object);
    }
    put(root, "assetValuations", assetValuations);

    QJsonArray liabilityBalances;
    for (const LiabilityBalances& balances : data.liabilityBalances) {
        QJsonObject object;
        put(object, "liabilityId", storageKeyText(balances.liabilityId));
        put(object, "points", pointsToJson(balances.points));
        liabilityBalances.append(object);
    }
    put(root, "liabilityBalances", liabilityBalances);

    QJsonArray monthFlows;
    for (const application::MonthFlowRecord& record : data.monthFlows)
        monthFlows.append(monthFlowToJson(record));
    put(root, "monthFlows", monthFlows);

    QJsonArray phaseHistory;
    for (const application::PhaseChange& change : data.phaseHistory)
        phaseHistory.append(phaseChangeToJson(change));
    put(root, "phaseHistory", phaseHistory);

    return toStd(QString::fromUtf8(QJsonDocument{root}.toJson(QJsonDocument::Indented)));
}

application::Result<BackupData> backupFromJson(std::string_view text)
{
    using Outcome = application::Result<BackupData>;

    QJsonParseError error{};
    const QByteArray bytes{text.data(), static_cast<qsizetype>(text.size())};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        return Outcome::err("backup: invalid JSON at offset " + std::to_string(error.offset) +
                            ": " + toStd(error.errorString()));

    if (!document.isObject())
        return Outcome::err("backup: the file is not a JSON object");
    const QJsonObject root = document.object();

    const application::Result<int> version = requireInt(root, "version", "backup");
    if (!version.ok())
        return Outcome::err(version.error());
    if (version.value() != kBackupVersion)
        return Outcome::err("backup: unsupported version " + std::to_string(version.value()) +
                            ", this build reads version " + std::to_string(kBackupVersion));

    BackupData data;

    const application::Result<domain::Profile> profile =
        profileFromJson(root.value(key("profile")), "backup: 'profile'");
    if (!profile.ok())
        return Outcome::err(profile.error());
    data.profile = profile.value();

    const application::Result<QJsonArray> assets =
        asArray(root.value(key("assets")), "backup: 'assets'");
    if (!assets.ok())
        return Outcome::err(assets.error());
    for (qsizetype i = 0; i < assets.value().size(); ++i) {
        const application::Result<domain::Asset> asset =
            assetFromJson(assets.value().at(i), indexed("assets", i));
        if (!asset.ok())
            return Outcome::err(asset.error());
        data.assets.push_back(asset.value());
    }

    const application::Result<QJsonArray> liabilities =
        asArray(root.value(key("liabilities")), "backup: 'liabilities'");
    if (!liabilities.ok())
        return Outcome::err(liabilities.error());
    for (qsizetype i = 0; i < liabilities.value().size(); ++i) {
        const application::Result<domain::Liability> liability =
            liabilityFromJson(liabilities.value().at(i), indexed("liabilities", i));
        if (!liability.ok())
            return Outcome::err(liability.error());
        data.liabilities.push_back(liability.value());
    }

    const application::Result<QJsonArray> plannedExpenses =
        asArray(root.value(key("plannedExpenses")), "backup: 'plannedExpenses'");
    if (!plannedExpenses.ok())
        return Outcome::err(plannedExpenses.error());
    for (qsizetype i = 0; i < plannedExpenses.value().size(); ++i) {
        const application::Result<domain::PlannedExpense> expense =
            plannedExpenseFromJson(plannedExpenses.value().at(i), indexed("plannedExpenses", i));
        if (!expense.ok())
            return Outcome::err(expense.error());
        data.plannedExpenses.push_back(expense.value());
    }

    const application::Result<QJsonArray> assetValuations =
        asArray(root.value(key("assetValuations")), "backup: 'assetValuations'");
    if (!assetValuations.ok())
        return Outcome::err(assetValuations.error());
    for (qsizetype i = 0; i < assetValuations.value().size(); ++i) {
        const std::string where = indexed("assetValuations", i);
        const application::Result<QJsonObject> object =
            asObject(assetValuations.value().at(i), where);
        if (!object.ok())
            return Outcome::err(object.error());

        AssetValuations valuations;
        const application::Result<QString> assetId =
            requireString(object.value(), "assetId", where);
        if (!assetId.ok())
            return Outcome::err(assetId.error());
        valuations.assetId = toStd(assetId.value());

        const application::Result<std::vector<MoneyPoint>> points =
            pointsFromJson(object.value().value(key("points")), where);
        if (!points.ok())
            return Outcome::err(points.error());
        valuations.points = points.value();

        data.assetValuations.push_back(std::move(valuations));
    }

    const application::Result<QJsonArray> liabilityBalances =
        asArray(root.value(key("liabilityBalances")), "backup: 'liabilityBalances'");
    if (!liabilityBalances.ok())
        return Outcome::err(liabilityBalances.error());
    for (qsizetype i = 0; i < liabilityBalances.value().size(); ++i) {
        const std::string where = indexed("liabilityBalances", i);
        const application::Result<QJsonObject> object =
            asObject(liabilityBalances.value().at(i), where);
        if (!object.ok())
            return Outcome::err(object.error());

        LiabilityBalances balances;
        const application::Result<QString> liabilityId =
            requireString(object.value(), "liabilityId", where);
        if (!liabilityId.ok())
            return Outcome::err(liabilityId.error());
        balances.liabilityId = toStd(liabilityId.value());

        const application::Result<std::vector<MoneyPoint>> points =
            pointsFromJson(object.value().value(key("points")), where);
        if (!points.ok())
            return Outcome::err(points.error());
        balances.points = points.value();

        data.liabilityBalances.push_back(std::move(balances));
    }

    const application::Result<QJsonArray> monthFlows =
        asArray(root.value(key("monthFlows")), "backup: 'monthFlows'");
    if (!monthFlows.ok())
        return Outcome::err(monthFlows.error());
    for (qsizetype i = 0; i < monthFlows.value().size(); ++i) {
        const application::Result<application::MonthFlowRecord> record =
            monthFlowFromJson(monthFlows.value().at(i), indexed("monthFlows", i));
        if (!record.ok())
            return Outcome::err(record.error());
        data.monthFlows.push_back(record.value());
    }

    const application::Result<QJsonArray> phaseHistory =
        asArray(root.value(key("phaseHistory")), "backup: 'phaseHistory'");
    if (!phaseHistory.ok())
        return Outcome::err(phaseHistory.error());
    for (qsizetype i = 0; i < phaseHistory.value().size(); ++i) {
        const application::Result<application::PhaseChange> change =
            phaseChangeFromJson(phaseHistory.value().at(i), indexed("phaseHistory", i));
        if (!change.ok())
            return Outcome::err(change.error());
        data.phaseHistory.push_back(change.value());
    }

    return Outcome::ok(std::move(data));
}

} // namespace hinkwise::infrastructure::json
