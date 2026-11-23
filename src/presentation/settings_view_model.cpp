// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_view_model.h"

#include "app_controller.h"
#include "catalog.h"
#include "domain/bucket.h"
#include "domain/targets.h"
#include "input_text.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace hinkwise::presentation {

namespace {

constexpr int kPhaseCount = static_cast<int>(domain::allPhases().size());

domain::Phase phaseAt(int index)
{
    return domain::allPhases()[static_cast<std::size_t>(std::clamp(index, 0, kPhaseCount - 1))];
}

int phaseIndexOf(domain::Phase phase)
{
    for (int index = 0; index < kPhaseCount; ++index) {
        if (phaseAt(index) == phase)
            return index;
    }
    return 0;
}

std::size_t bucketIndex(domain::Bucket bucket)
{
    return static_cast<std::size_t>(bucket);
}

/// "0,5", "6", "25": a multiple as the model writes it, without the trailing
/// zeroes a percentage formatter would add.
QString multipleText(domain::Percentage multiple)
{
    return QLocale{}.toString(multiple.basisPoints() / 10000.0, 'g', 3);
}

/// "10 %", for a cap that is a share rather than an amount.
QString percentText(domain::Percentage percentage)
{
    return SettingsViewModel::tr("%1 %").arg(
        QLocale{}.toString(percentage.basisPoints() / 100.0, 'g', 3));
}

/// The text a rate field holds: what parsePercentageInput() reads back.
QString rateText(domain::Percentage percentage)
{
    return QLocale{}.toString(percentage.basisPoints() / 100.0, 'f', 2);
}

QString currencyNameOf(domain::Currency currency)
{
    switch (currency) {
    case domain::Currency::Sek:
        return SettingsViewModel::tr("Swedish krona");
    case domain::Currency::Eur:
        return SettingsViewModel::tr("Euro");
    case domain::Currency::Usd:
        return SettingsViewModel::tr("US dollar");
    }
    return {};
}

bool sameJurisdiction(const domain::Jurisdiction& left, const domain::Jurisdiction& right)
{
    return left.safeWithdrawalRate == right.safeWithdrawalRate &&
           left.capitalTaxDrag == right.capitalTaxDrag &&
           left.fiMultiplierLow == right.fiMultiplierLow &&
           left.fiMultiplierDefault == right.fiMultiplierDefault &&
           left.fiMultiplierHigh == right.fiMultiplierHigh;
}

/// The sizing rule a phase states for a bucket, in words: "0,5–6 months of
/// expenses", "1–5 years of expenses". The band is the domain's; only the
/// wording is here.
///
/// The one part of the rule left out is the lump-sum clause, because the band
/// this words is sized from today's expenses alone: planned purchases enter the
/// buffer on the year-end review, which is where the household sees what they
/// do to it. Naming them here would claim they were in the figures below.
QString ruleText(const domain::BucketTarget& rule)
{
    const QString phrasing = rule.basis == domain::ExpenseBasis::Monthly
                                 ? SettingsViewModel::tr("%1–%2 months of expenses")
                                 : SettingsViewModel::tr("%1–%2 years of expenses");
    return phrasing.arg(multipleText(rule.multiple.low), multipleText(rule.multiple.high));
}

QString moneyRangeText(const domain::MoneyRange& band)
{
    return SettingsViewModel::tr("%1 – %2").arg(formatMoney(band.low), formatMoney(band.high));
}

/// What a bucket is sized at in one phase, in words and in the household's own
/// money. Play is the special case the model insists on: a ceiling is not a
/// target, so a bucket with no target still says what limits it.
QString bucketText(domain::Bucket bucket, const domain::ResolvedTarget& target,
                   const domain::BucketTarget& rule)
{
    QString text;
    switch (target.kind) {
    case domain::BucketTarget::Kind::MultipleOfExpenses:
        text = SettingsViewModel::tr("%1 · %2").arg(ruleText(rule), moneyRangeText(target.band));
        break;
    case domain::BucketTarget::Kind::AsLargeAsPossible:
        text = SettingsViewModel::tr("As large as possible");
        break;
    case domain::BucketTarget::Kind::NoTarget:
        if (bucket == domain::Bucket::Growth)
            text = SettingsViewModel::tr("Optional");
        else if (!rule.cap.has_value())
            text = SettingsViewModel::tr("Not a target");
        break;
    }

    if (!rule.cap.has_value())
        return text;

    const QString cap =
        SettingsViewModel::tr("Cap at %1 of %2, not a target")
            .arg(percentText(rule.cap->share), catalog::bucketLabel(rule.cap->bucket));
    return text.isEmpty() ? cap : SettingsViewModel::tr("%1 · %2").arg(text, cap);
}

/// How far the buffer is from the point in its band this household chose. A
/// balance past the point is covered, not a negative gap.
QString gapText(const domain::ResolvedTarget& target, domain::Money balance)
{
    if (!target.hasTarget())
        return SettingsViewModel::tr("No target");
    const domain::Money gap = target.point - balance;
    if (!gap.isPositive())
        return SettingsViewModel::tr("Covered");
    return SettingsViewModel::tr("%1 short").arg(formatMoney(gap));
}

QString flowText(domain::Phase phase)
{
    if (domain::policyFor(phase).flow() == domain::FlowDirection::IntoBuckets)
        return SettingsViewModel::tr("Money fills the buckets");
    return SettingsViewModel::tr("The buffer is drawn down and refilled from preserve");
}

/// A multiplier field, read as the whole number of years' expenses it is.
struct MultiplierInput {
    bool ok = false;
    int value = 0;
    QString error;
};

MultiplierInput parseMultiplier(const QString& text)
{
    const QString cleaned = text.simplified().remove(QLatin1Char(' '));
    bool ok = false;
    const int value = cleaned.toInt(&ok);
    if (!ok || value < 1 || value > 200)
        return MultiplierInput{false, 0,
                               SettingsViewModel::tr("A multiplier is a whole number of years' "
                                                     "expenses, from 1 to 200.")};
    return MultiplierInput{true, value, {}};
}

} // namespace

// ---------------------------------------------------------------------------
// The phase comparison
// ---------------------------------------------------------------------------

QVector<PhaseComparisonRow> phaseComparisonOf(const application::DashboardSnapshot& snapshot,
                                              domain::Phase next)
{
    const domain::Phase now = snapshot.profile.phase;
    const domain::BucketTargets& nowRules = domain::policyFor(now).targets();
    const domain::BucketTargets& nextRules = domain::policyFor(next).targets();

    // The same inputs the dashboard sizes with, so the "after" column is what
    // the buckets would show once the phase is changed and nothing else is.
    domain::TargetInputs inputs{snapshot.profile.baseCurrency};
    inputs.monthlyExpenses = snapshot.facts.flow.needs + snapshot.facts.flow.wants;
    inputs.annualExpenses =
        inputs.monthlyExpenses.scaledBy(domain::Percentage::fromBasisPoints(120000));
    inputs.growthBalance = snapshot.facts.sheet.perBucket[bucketIndex(domain::Bucket::Growth)];
    inputs.riskTolerance = snapshot.profile.riskTolerance;
    const domain::ResolvedTargets nextTargets = domain::resolve(nextRules, inputs);
    const domain::ResolvedTargets& nowTargets = snapshot.facts.targets;

    const domain::Money bufferBalance =
        snapshot.facts.sheet.perBucket[bucketIndex(domain::Bucket::Buffer)];

    const auto row = [](const QString& label, const QString& before, const QString& after) {
        return PhaseComparisonRow{label, before, after, before != after};
    };

    QVector<PhaseComparisonRow> rows;
    rows.append(
        row(SettingsViewModel::tr("Buffer target"),
            bucketText(domain::Bucket::Buffer, nowTargets[bucketIndex(domain::Bucket::Buffer)],
                       nowRules[domain::Bucket::Buffer]),
            bucketText(domain::Bucket::Buffer, nextTargets[bucketIndex(domain::Bucket::Buffer)],
                       nextRules[domain::Bucket::Buffer])));
    rows.append(row(SettingsViewModel::tr("Buffer gap today"),
                    gapText(nowTargets[bucketIndex(domain::Bucket::Buffer)], bufferBalance),
                    gapText(nextTargets[bucketIndex(domain::Bucket::Buffer)], bufferBalance)));
    rows.append(
        row(SettingsViewModel::tr("Preserve target"),
            bucketText(domain::Bucket::Preserve, nowTargets[bucketIndex(domain::Bucket::Preserve)],
                       nowRules[domain::Bucket::Preserve]),
            bucketText(domain::Bucket::Preserve, nextTargets[bucketIndex(domain::Bucket::Preserve)],
                       nextRules[domain::Bucket::Preserve])));
    rows.append(
        row(SettingsViewModel::tr("Growth"),
            bucketText(domain::Bucket::Growth, nowTargets[bucketIndex(domain::Bucket::Growth)],
                       nowRules[domain::Bucket::Growth]),
            bucketText(domain::Bucket::Growth, nextTargets[bucketIndex(domain::Bucket::Growth)],
                       nextRules[domain::Bucket::Growth])));
    rows.append(row(SettingsViewModel::tr("Play"),
                    bucketText(domain::Bucket::Play, nowTargets[bucketIndex(domain::Bucket::Play)],
                               nowRules[domain::Bucket::Play]),
                    bucketText(domain::Bucket::Play, nextTargets[bucketIndex(domain::Bucket::Play)],
                               nextRules[domain::Bucket::Play])));
    rows.append(row(SettingsViewModel::tr("Where money moves"), flowText(now), flowText(next)));
    return rows;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

SettingsViewModel::SettingsViewModel(QObject* parent)
    : QObject(parent)
{
    if (AppController* controller = AppController::instance()) {
        connect(controller, &AppController::dataChanged, this, &SettingsViewModel::reload);
    }
    reload();
}

void SettingsViewModel::reload()
{
    const AppController* controller = AppController::instance();
    apply(controller != nullptr ? controller->dashboardSnapshot()
                                : application::DashboardSnapshot{});
}

void SettingsViewModel::apply(const application::DashboardSnapshot& snapshot)
{
    // Read before the profile is replaced: what the page was showing is what
    // decides whether the fields need refilling.
    const domain::Jurisdiction previousJurisdiction = jurisdiction();
    const std::string previousName = m_profile.displayName;
    const bool nameWasDirty = displayNameDirty();
    const bool jurisdictionWasDirty = jurisdictionDirty();
    const bool bufferWasDirty = bufferTargetDirty();

    m_hasProfile = snapshot.hasProfile;
    m_snapshot = snapshot;
    m_profile = snapshot.profile;
    m_phaseIndex = phaseIndexOf(m_profile.phase);

    // A field keeps what was typed until the household saves or discards it.
    // A reload that arrives mid-edit must never throw a draft away.
    if (!nameWasDirty && previousName != m_profile.displayName)
        m_displayNameText = QString::fromStdString(m_profile.displayName);

    if (!m_jurisdictionFilled) {
        fillJurisdictionTexts();
        m_jurisdictionFilled = true;
    } else if (!jurisdictionWasDirty && !sameJurisdiction(previousJurisdiction, jurisdiction())) {
        fillJurisdictionTexts();
    }

    if (!bufferWasDirty)
        m_bufferTargetPosition = m_profile.riskTolerance.basisPoints();

    m_phaseHistory = phaseHistoryRows();
    emit changed();
}

QVariantList SettingsViewModel::phaseChoices() const
{
    QVariantList choices;
    for (int index = 0; index < kPhaseCount; ++index) {
        choices.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("label"), QString::fromUtf8(domain::labelOf(phaseAt(index)))},
        });
    }
    return choices;
}

QString SettingsViewModel::phaseLabel() const
{
    if (!m_hasProfile)
        return {};
    return QString::fromUtf8(domain::labelOf(m_profile.phase));
}

QString SettingsViewModel::pendingPhaseLabel() const
{
    if (!comparisonVisible())
        return {};
    return QString::fromUtf8(domain::labelOf(phaseAt(m_pendingPhaseIndex)));
}

QVariantList SettingsViewModel::comparisonRows() const
{
    QVariantList rows;
    if (!comparisonVisible())
        return rows;

    const QVector<PhaseComparisonRow> comparison =
        phaseComparisonOf(m_snapshot, phaseAt(m_pendingPhaseIndex));
    rows.reserve(comparison.size());
    for (const PhaseComparisonRow& row : comparison) {
        rows.append(QVariantMap{{QStringLiteral("label"), row.label},
                                {QStringLiteral("before"), row.before},
                                {QStringLiteral("after"), row.after},
                                {QStringLiteral("changed"), row.changed}});
    }
    return rows;
}

bool SettingsViewModel::comparisonChanges() const
{
    if (!comparisonVisible())
        return false;
    const QVector<PhaseComparisonRow> comparison =
        phaseComparisonOf(m_snapshot, phaseAt(m_pendingPhaseIndex));
    return std::any_of(comparison.cbegin(), comparison.cend(),
                       [](const PhaseComparisonRow& row) { return row.changed; });
}

void SettingsViewModel::beginPhaseChange(int index)
{
    m_phaseError.clear();
    if (!m_hasProfile) {
        emit changed();
        return;
    }

    const int clamped = std::clamp(index, 0, kPhaseCount - 1);
    // Choosing the phase the household is already in opens nothing: there is
    // no before and after to show.
    m_pendingPhaseIndex = clamped == m_phaseIndex ? -1 : clamped;
    emit changed();
}

QString SettingsViewModel::confirmPhaseChange()
{
    if (!comparisonVisible())
        return {};

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_phaseError = tr("The database is not open.");
        emit changed();
        return m_phaseError;
    }

    const application::Status applied = controller->changePhase(phaseAt(m_pendingPhaseIndex));
    if (!applied.ok()) {
        m_phaseError = QString::fromStdString(applied.error());
        emit changed();
        return m_phaseError;
    }

    m_pendingPhaseIndex = -1;
    m_phaseError.clear();
    emit changed();
    return {};
}

void SettingsViewModel::cancelPhaseChange()
{
    m_pendingPhaseIndex = -1;
    m_phaseError.clear();
    emit changed();
}

bool SettingsViewModel::displayNameDirty() const
{
    return m_displayNameText.trimmed() != QString::fromStdString(m_profile.displayName);
}

bool SettingsViewModel::bufferTargetDirty() const
{
    return m_bufferTargetPosition != m_profile.riskTolerance.basisPoints();
}

QString SettingsViewModel::bufferTargetPreview() const
{
    if (!m_hasProfile)
        return {};

    // The point is resolved with the same inputs the dashboard uses, so the
    // number here is the number the buckets will show once it is saved.
    const domain::Money monthlyExpenses = m_snapshot.facts.flow.needs + m_snapshot.facts.flow.wants;
    if (!monthlyExpenses.isPositive())
        return tr("Record a month to see this point in money.");

    const domain::Money annualExpenses =
        monthlyExpenses.scaledBy(domain::Percentage::fromBasisPoints(120000));
    domain::TargetInputs inputs{m_profile.baseCurrency};
    inputs.monthlyExpenses = monthlyExpenses;
    inputs.annualExpenses = annualExpenses;
    inputs.growthBalance =
        m_snapshot.facts.sheet.perBucket[static_cast<std::size_t>(domain::Bucket::Growth)];
    inputs.riskTolerance = domain::Percentage::fromBasisPoints(m_bufferTargetPosition);

    const domain::BucketTargets& rules = domain::policyFor(m_profile.phase).targets();
    const domain::ResolvedTargets resolved = domain::resolve(rules, inputs);
    const domain::ResolvedTarget& buffer =
        resolved[static_cast<std::size_t>(domain::Bucket::Buffer)];
    if (!buffer.hasTarget())
        return tr("This phase gives the buffer no target.");

    const QLocale locale;
    const bool annual = rules[domain::Bucket::Buffer].basis == domain::ExpenseBasis::Annual;
    const domain::Money unit = annual ? annualExpenses : monthlyExpenses;
    const double count =
        static_cast<double>(buffer.point.minorUnits()) / static_cast<double>(unit.minorUnits());
    const QString equivalent = annual ? tr("%1 years").arg(locale.toString(count, 'f', 1))
                                      : tr("%1 months").arg(locale.toString(count, 'f', 1));
    return tr("%1 · %2").arg(formatMoney(buffer.point), equivalent);
}

void SettingsViewModel::setBufferTargetPosition(int position)
{
    const int clamped = std::clamp(position, 0, bufferTargetMaximum());
    if (clamped == m_bufferTargetPosition)
        return;
    m_bufferTargetPosition = clamped;
    m_bufferTargetError.clear();
    emit changed();
}

QString SettingsViewModel::currencyCode() const
{
    return QString::fromLatin1(domain::isoCodeOf(m_profile.baseCurrency));
}

QString SettingsViewModel::currencyName() const
{
    return currencyNameOf(m_profile.baseCurrency);
}

domain::Jurisdiction SettingsViewModel::jurisdiction() const
{
    // A profile saved before the jurisdiction was ever edited carries no id,
    // and every other page falls back to the Swedish constants for it, so the
    // fields have to show those same constants.
    if (m_profile.jurisdiction.id.empty())
        return domain::swedishJurisdiction();
    return m_profile.jurisdiction;
}

void SettingsViewModel::fillJurisdictionTexts()
{
    const domain::Jurisdiction current = jurisdiction();
    m_safeWithdrawalRateText = rateText(current.safeWithdrawalRate);
    m_capitalTaxDragText = rateText(current.capitalTaxDrag);
    m_fiMultiplierLowText = QString::number(current.fiMultiplierLow);
    m_fiMultiplierDefaultText = QString::number(current.fiMultiplierDefault);
    m_fiMultiplierHighText = QString::number(current.fiMultiplierHigh);
    m_safeWithdrawalRateError.clear();
    m_capitalTaxDragError.clear();
    m_multiplierError.clear();
}

bool SettingsViewModel::jurisdictionDirty() const
{
    const domain::Jurisdiction current = jurisdiction();
    return m_safeWithdrawalRateText != rateText(current.safeWithdrawalRate) ||
           m_capitalTaxDragText != rateText(current.capitalTaxDrag) ||
           m_fiMultiplierLowText != QString::number(current.fiMultiplierLow) ||
           m_fiMultiplierDefaultText != QString::number(current.fiMultiplierDefault) ||
           m_fiMultiplierHighText != QString::number(current.fiMultiplierHigh);
}

void SettingsViewModel::setDisplayNameText(const QString& text)
{
    if (text == m_displayNameText)
        return;
    m_displayNameText = text;
    emit changed();
}

void SettingsViewModel::setSafeWithdrawalRateText(const QString& text)
{
    if (text == m_safeWithdrawalRateText)
        return;
    m_safeWithdrawalRateText = text;
    emit changed();
}

void SettingsViewModel::setCapitalTaxDragText(const QString& text)
{
    if (text == m_capitalTaxDragText)
        return;
    m_capitalTaxDragText = text;
    emit changed();
}

void SettingsViewModel::setFiMultiplierLowText(const QString& text)
{
    if (text == m_fiMultiplierLowText)
        return;
    m_fiMultiplierLowText = text;
    emit changed();
}

void SettingsViewModel::setFiMultiplierDefaultText(const QString& text)
{
    if (text == m_fiMultiplierDefaultText)
        return;
    m_fiMultiplierDefaultText = text;
    emit changed();
}

void SettingsViewModel::setFiMultiplierHighText(const QString& text)
{
    if (text == m_fiMultiplierHighText)
        return;
    m_fiMultiplierHighText = text;
    emit changed();
}

QString SettingsViewModel::saveDisplayName()
{
    const QString name = m_displayNameText.trimmed();
    if (name.isEmpty()) {
        m_displayNameError = tr("A household needs a name.");
        emit changed();
        return m_displayNameError;
    }

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_displayNameError = tr("The database is not open.");
        emit changed();
        return m_displayNameError;
    }

    domain::Profile updated = m_profile;
    updated.displayName = name.toStdString();
    const application::Status saved = controller->saveProfile(updated);
    if (!saved.ok()) {
        m_displayNameError = QString::fromStdString(saved.error());
        emit changed();
        return m_displayNameError;
    }

    // The stored name is what the field shows after a save, trimmed: the
    // draft may have carried spaces that were not part of the name.
    m_displayNameText = QString::fromStdString(m_profile.displayName);
    m_displayNameError.clear();
    emit changed();
    return {};
}

void SettingsViewModel::discardDisplayName()
{
    m_displayNameText = QString::fromStdString(m_profile.displayName);
    m_displayNameError.clear();
    emit changed();
}

QString SettingsViewModel::saveJurisdiction()
{
    m_safeWithdrawalRateError.clear();
    m_capitalTaxDragError.clear();
    m_multiplierError.clear();

    const PercentageInput rate = parsePercentageInput(m_safeWithdrawalRateText);
    if (!rate.ok)
        m_safeWithdrawalRateError = rate.error;
    else if (rate.value.isZero())
        m_safeWithdrawalRateError = tr("A withdrawal rate has to be above zero.");

    // A capital tax drag of zero is a real assumption, not a missing value.
    const PercentageInput drag = parsePercentageInput(m_capitalTaxDragText);
    if (!drag.ok)
        m_capitalTaxDragError = drag.error;

    const MultiplierInput low = parseMultiplier(m_fiMultiplierLowText);
    const MultiplierInput middle = parseMultiplier(m_fiMultiplierDefaultText);
    const MultiplierInput high = parseMultiplier(m_fiMultiplierHighText);
    if (!low.ok || !middle.ok || !high.ok) {
        if (!low.ok)
            m_multiplierError = low.error;
        else if (!middle.ok)
            m_multiplierError = middle.error;
        else
            m_multiplierError = high.error;
    } else if (low.value > middle.value || middle.value > high.value) {
        m_multiplierError = tr("The multipliers have to run low, default, high.");
    }

    if (!m_safeWithdrawalRateError.isEmpty() || !m_capitalTaxDragError.isEmpty() ||
        !m_multiplierError.isEmpty()) {
        emit changed();
        if (!m_safeWithdrawalRateError.isEmpty())
            return m_safeWithdrawalRateError;
        if (!m_capitalTaxDragError.isEmpty())
            return m_capitalTaxDragError;
        return m_multiplierError;
    }

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_multiplierError = tr("The database is not open.");
        emit changed();
        return m_multiplierError;
    }

    domain::Profile updated = m_profile;
    updated.jurisdiction =
        domain::customJurisdiction(rate.value, drag.value, low.value, middle.value, high.value);
    const application::Status saved = controller->saveProfile(updated);
    if (!saved.ok()) {
        m_multiplierError = QString::fromStdString(saved.error());
        emit changed();
        return m_multiplierError;
    }

    // Show the stored values in their canonical spelling, not whatever the
    // household happened to type. The reload that saveProfile() announced has
    // already run and deliberately kept the draft because it looked dirty.
    fillJurisdictionTexts();
    emit changed();
    return {};
}

void SettingsViewModel::discardJurisdiction()
{
    fillJurisdictionTexts();
    emit changed();
}

QString SettingsViewModel::saveBufferTarget()
{
    m_bufferTargetError.clear();

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_bufferTargetError = tr("The database is not open.");
        emit changed();
        return m_bufferTargetError;
    }

    domain::Profile updated = m_profile;
    updated.riskTolerance = domain::Percentage::fromBasisPoints(m_bufferTargetPosition);
    const application::Status saved = controller->saveProfile(updated);
    if (!saved.ok()) {
        m_bufferTargetError = QString::fromStdString(saved.error());
        emit changed();
        return m_bufferTargetError;
    }

    m_bufferTargetError.clear();
    m_bufferTargetPosition = updated.riskTolerance.basisPoints();
    emit changed();
    return {};
}

void SettingsViewModel::discardBufferTarget()
{
    m_bufferTargetPosition = m_profile.riskTolerance.basisPoints();
    m_bufferTargetError.clear();
    emit changed();
}

void SettingsViewModel::resetJurisdictionToSwedishDefaults()
{
    const domain::Jurisdiction sweden = domain::swedishJurisdiction();
    m_safeWithdrawalRateText = rateText(sweden.safeWithdrawalRate);
    m_capitalTaxDragText = rateText(sweden.capitalTaxDrag);
    m_fiMultiplierLowText = QString::number(sweden.fiMultiplierLow);
    m_fiMultiplierDefaultText = QString::number(sweden.fiMultiplierDefault);
    m_fiMultiplierHighText = QString::number(sweden.fiMultiplierHigh);
    m_safeWithdrawalRateError.clear();
    m_capitalTaxDragError.clear();
    m_multiplierError.clear();
    emit changed();
}

QString SettingsViewModel::databasePath() const
{
    const AppController* controller = AppController::instance();
    if (controller == nullptr)
        return {};
    return controller->databasePath();
}

QUrl SettingsViewModel::backupFileUrl() const
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();

    const QString name =
        tr("hinkwise-backup-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
    return QUrl::fromLocalFile(QDir{directory}.filePath(name));
}

QString SettingsViewModel::exportBackup(const QUrl& fileUrl)
{
    m_backupMessage.clear();
    m_backupError.clear();

    const QString path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        m_backupError = tr("Choose a file on this machine to write the backup to.");
        emit changed();
        return m_backupError;
    }

    AppController* controller = AppController::instance();
    if (controller == nullptr) {
        m_backupError = tr("The database is not open.");
        emit changed();
        return m_backupError;
    }

    const application::Result<int> written = controller->exportBackup(path);
    if (!written.ok()) {
        m_backupError = QString::fromStdString(written.error());
        emit changed();
        return m_backupError;
    }

    m_backupMessage = written.value() == 1
                          ? tr("Wrote 1 record to %1").arg(path)
                          : tr("Wrote %1 records to %2").arg(written.value()).arg(path);
    emit changed();
    return {};
}

QVariantList SettingsViewModel::phaseHistoryRows() const
{
    const AppController* controller = AppController::instance();
    if (controller == nullptr)
        return {};

    const std::vector<application::PhaseChange> history = controller->phaseHistory();
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(history.size()));
    // Newest first: the last change is the one a household looks for.
    for (auto entry = history.crbegin(); entry != history.crend(); ++entry) {
        const QString stamped = QString::fromStdString(entry->changedAt);
        const QDateTime moment = QDateTime::fromString(stamped, Qt::ISODate);
        rows.append(QVariantMap{
            {QStringLiteral("label"), QString::fromUtf8(domain::labelOf(entry->phase))},
            {QStringLiteral("when"),
             moment.isValid() ? QLocale{}.toString(moment.toLocalTime(), QLocale::ShortFormat)
                              : stamped},
        });
    }
    return rows;
}

QString SettingsViewModel::appVersion() const
{
    return QCoreApplication::applicationVersion();
}

QString SettingsViewModel::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

} // namespace hinkwise::presentation
