// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_text.h"

#include <QChar>
#include <QCoreApplication>
#include <QDate>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <limits>

namespace hinkwise::presentation {

namespace {

QString message(const char* source)
{
    return QCoreApplication::translate("InputText", source);
}

bool isSeparator(QChar character)
{
    return character == QLatin1Char(',') || character == QLatin1Char('.');
}

/// Why a string could not be read as a number. The public parsers turn these
/// into the sentence the field shows.
enum class NumberError {
    None,
    Empty,
    NotANumber,
    TooManyDecimals,
    TooLarge,
};

struct NumberReading {
    NumberError error = NumberError::None;
    /// The value in hundredths of the unit: for money, minor units; for a
    /// percentage, basis points.
    std::int64_t units = 0;
};

bool accumulate(const QString& digits, std::int64_t* total)
{
    for (const QChar character : digits) {
        const int value = character.digitValue();
        if (value < 0)
            return false;
        if (*total > (std::numeric_limits<std::int64_t>::max() - value) / 10)
            return false;
        *total = *total * 10 + value;
    }
    return true;
}

/// Reads the digits of a number written with `decimals` decimal places.
///
/// The separators are read without asking the locale first, because a household
/// moving between a Swedish keyboard and an English one types both conventions.
/// The locale decides formatting, which is the part that has to look right.
NumberReading readNumber(const QString& text, int decimals)
{
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar character : text) {
        if (!character.isSpace())
            cleaned.append(character);
    }

    if (cleaned.isEmpty())
        return {NumberError::Empty, 0};

    bool anyDigit = false;
    for (const QChar character : cleaned) {
        if (character.isDigit())
            anyDigit = true;
        else if (!isSeparator(character))
            return {NumberError::NotANumber, 0};
    }
    if (!anyDigit)
        return {NumberError::NotANumber, 0};

    // The locale's own decimal point wins where it appears, so a field the
    // household can see formatted one way is read back the same way. Without
    // one, a single separator followed by fewer digits than a thousands group
    // is the decimal point; the whole string is otherwise an integer.
    const QLocale locale;
    qsizetype decimalAt = cleaned.lastIndexOf(locale.decimalPoint());
    if (decimalAt < 0) {
        qsizetype candidate = -1;
        int separators = 0;
        for (qsizetype index = 0; index < cleaned.size(); ++index) {
            if (isSeparator(cleaned.at(index))) {
                candidate = index;
                ++separators;
            }
        }
        if (separators == 1 && cleaned.size() - candidate - 1 <= decimals)
            decimalAt = candidate;
    }

    const QString majorPart = decimalAt < 0 ? cleaned : cleaned.left(decimalAt);
    const QString minorPart = decimalAt < 0 ? QString{} : cleaned.mid(decimalAt + 1);

    if (minorPart.size() > decimals)
        return {NumberError::TooManyDecimals, 0};

    // Thousands groups, when there are any: the first may be one to three
    // digits, every later one must be exactly three. A number written without
    // separators is read as it stands, whatever its length.
    QStringList groups;
    QString current;
    for (const QChar character : majorPart) {
        if (isSeparator(character)) {
            groups.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    groups.append(current);
    if (groups.size() > 1) {
        for (qsizetype index = 0; index < groups.size(); ++index) {
            const qsizetype length = groups.at(index).size();
            const bool valid = index == 0 ? length >= 1 && length <= 3 : length == 3;
            if (!valid)
                return {NumberError::NotANumber, 0};
        }
    }

    std::int64_t units = 0;
    for (const QString& group : groups) {
        if (!accumulate(group, &units))
            return {NumberError::TooLarge, 0};
    }

    std::int64_t scale = 1;
    for (int index = 0; index < decimals; ++index)
        scale *= 10;
    if (units > std::numeric_limits<std::int64_t>::max() / scale)
        return {NumberError::TooLarge, 0};
    units *= scale;

    std::int64_t fraction = 0;
    if (!accumulate(minorPart, &fraction))
        return {NumberError::TooLarge, 0};
    for (qsizetype index = minorPart.size(); index < decimals; ++index)
        fraction *= 10;
    // The whole units were checked against the scaled maximum above, but the
    // fraction is added afterwards. Adding it unchecked would wrap at the
    // int64 limit and store a small amount from a huge input.
    if (fraction > std::numeric_limits<std::int64_t>::max() - units)
        return {NumberError::TooLarge, 0};
    units += fraction;

    return {NumberError::None, units};
}

QString errorFor(NumberError error, const QString& text, int decimals, bool money)
{
    switch (error) {
    case NumberError::Empty:
        return money ? message("Enter an amount.") : message("Enter a rate.");
    case NumberError::NotANumber:
        return money ? message("\"%1\" is not an amount.").arg(text)
                     : message("\"%1\" is not a rate.").arg(text);
    case NumberError::TooManyDecimals:
        return message("Use at most %1 decimals.").arg(decimals);
    case NumberError::TooLarge:
        return message("That number is too large to record.");
    case NumberError::None:
        break;
    }
    return {};
}

} // namespace

QString formatMoney(const domain::Money& amount)
{
    const QLocale locale;
    std::int64_t scale = 1;
    for (int index = 0; index < domain::traitsOf(amount.currency()).minorDigits; ++index)
        scale *= 10;
    return locale.toCurrencyString(static_cast<double>(amount.minorUnits()) /
                                       static_cast<double>(scale),
                                   QString::fromLatin1(domain::isoCodeOf(amount.currency())));
}

QString formatMoneyPlain(const domain::Money& amount)
{
    const QLocale locale;
    const int decimals = domain::traitsOf(amount.currency()).minorDigits;
    const bool negative = amount.isNegative();
    const std::int64_t magnitude = negative ? -amount.minorUnits() : amount.minorUnits();

    std::int64_t scale = 1;
    for (int index = 0; index < decimals; ++index)
        scale *= 10;

    QString text = locale.toString(static_cast<qlonglong>(magnitude / scale));
    if (decimals > 0) {
        const QString fraction = QString::number(static_cast<qlonglong>(magnitude % scale))
                                     .rightJustified(decimals, QLatin1Char('0'));
        text += locale.decimalPoint();
        text += fraction;
    }
    return negative ? QString{QLatin1Char('-')} + text : text;
}

QString formatPercentage(domain::Percentage percentage)
{
    const QLocale locale;
    return message("%1 %").arg(
        locale.toString(static_cast<double>(percentage.basisPoints()) / 100.0, 'f', 2));
}

QString formatPercentagePlain(domain::Percentage percentage)
{
    const QLocale locale;
    return locale.toString(static_cast<double>(percentage.basisPoints()) / 100.0, 'f', 2);
}

QString formatMonth(domain::YearMonth month, const QString& format)
{
    const QDate date{month.year(), static_cast<int>(month.month()), 1};
    return QLocale{}.toString(date, format);
}

MoneyInput parseMoneyInput(const QString& text, domain::Currency currency)
{
    const NumberReading reading = readNumber(text, domain::traitsOf(currency).minorDigits);
    if (reading.error != NumberError::None)
        return MoneyInput{
            false,
            {},
            errorFor(reading.error, text.trimmed(), domain::traitsOf(currency).minorDigits, true),
            {}};

    MoneyInput result;
    result.ok = true;
    result.value = domain::Money{reading.units, currency};
    result.label = formatMoney(result.value);
    return result;
}

PercentageInput parsePercentageInput(const QString& text)
{
    // A rate is stored in basis points, so "4,5" has to arrive at 450: read it
    // as a number with two decimals and take the hundredths as basis points.
    const NumberReading reading = readNumber(text, 2);
    if (reading.error != NumberError::None)
        return PercentageInput{false, {}, errorFor(reading.error, text.trimmed(), 2, false), {}};
    if (reading.units > std::numeric_limits<std::int32_t>::max())
        return PercentageInput{false, {}, errorFor(NumberError::TooLarge, {}, 2, false), {}};

    PercentageInput result;
    result.ok = true;
    result.value = domain::Percentage::fromBasisPoints(static_cast<std::int32_t>(reading.units));
    result.label = formatPercentage(result.value);
    return result;
}

TermYearsInput parseTermYearsInput(const QString& text)
{
    const NumberReading reading = readNumber(text, 2);
    if (reading.error != NumberError::None) {
        if (reading.error == NumberError::Empty)
            return {false, 0, message("Enter the years left, or leave the field empty.")};
        if (reading.error == NumberError::NotANumber)
            return {false, 0, message("\"%1\" is not a number of years.").arg(text.trimmed())};
        return {false, 0, errorFor(reading.error, text.trimmed(), 2, false)};
    }

    // Years are stored as months. The product by twelve is checked before it is
    // taken, because readNumber() accepts amounts up to the int64 limit and a
    // term that large is broken input rather than a long loan.
    if (reading.units > (std::numeric_limits<std::int64_t>::max() - 50) / 12)
        return {false, 0, errorFor(NumberError::TooLarge, {}, 2, false)};
    std::int64_t months = (reading.units * 12 + 50) / 100;
    // A stated term shorter than half a month is still a term; rounding it away
    // would silently turn it into "not stated".
    if (months == 0 && reading.units > 0)
        months = 1;
    if (months > std::numeric_limits<int>::max())
        return {false, 0, errorFor(NumberError::TooLarge, {}, 2, false)};

    return {true, static_cast<int>(months), {}};
}

QString formatTermYears(int months)
{
    if (months <= 0)
        return {};

    const std::int64_t hundredths = (static_cast<std::int64_t>(months) * 100 + 6) / 12;
    const std::int64_t whole = hundredths / 100;
    const int fraction = static_cast<int>(hundredths % 100);
    if (fraction == 0)
        return QString::number(whole);

    QString fractionText = QString::number(fraction).rightJustified(2, QLatin1Char('0'));
    if (fraction % 10 == 0)
        fractionText.chop(1);
    return QString::number(whole) + QLocale{}.decimalPoint() + fractionText;
}

} // namespace hinkwise::presentation
