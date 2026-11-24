// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/interest_rate.h"
#include "domain/money.h"
#include "domain/percentage.h"
#include "domain/year_month.h"

#include <QString>

namespace hinkwise::presentation {

/// The text edge of money: where an amount crosses between a human and the
/// model.
///
/// QML never adds, divides or parses an amount. It hands the raw string to a
/// view model, which reads it with parseMoneyInput(), and it shows what
/// formatMoney() returns. From the moment it is understood the amount is an
/// integer number of minor units.

/// The amount with its ISO code, in the user's own notation: the dashboard and
/// every list use this one spelling.
QString formatMoney(const domain::Money& amount);

/// The amount without the currency symbol, so that text can be put back into a
/// field and read by parseMoneyInput() unchanged.
QString formatMoneyPlain(const domain::Money& amount);

/// "4,50 %" - an interest rate in the user's own notation.
QString formatPercentage(domain::Percentage percentage);

/// The rate without the percent sign, so that text can be put back into a
/// field and read by parsePercentageInput() unchanged.
QString formatPercentagePlain(domain::Percentage percentage);

/// "2,655 %" - a loan's interest rate in the user's own notation, with the
/// three decimals a lender quotes.
QString formatInterestRate(domain::InterestRate rate);

/// The loan rate without the percent sign, so that text can be put back into a
/// field and read by parseInterestRateInput() unchanged.
QString formatInterestRatePlain(domain::InterestRate rate);

/// A month in the user's own notation, in the format the caller asks for:
/// "October 2026" for a sentence, "Oct" for an axis. Every screen names a
/// month the same way, so two pages cannot spell the same month differently.
QString formatMonth(domain::YearMonth month, const QString& format);

/// A string that has been read as money, or the reason it could not be.
struct MoneyInput {
    bool ok = false;
    domain::Money value{};
    /// What went wrong, in a sentence fit for the field it was typed into.
    /// Empty when ok.
    QString error;
    /// The amount as a screen shows it. Empty when ok is false.
    QString label;
};

/// Reads what the household typed into an amount field.
///
/// Digits, spaces, commas and dots are accepted; a space is grouping and never
/// a sign, so a negative amount cannot be entered. A separator is the decimal
/// point unless it is followed by three digits, in which case it was a
/// thousands group: "1 234,56", "1,234.56" and "1.234,56" all mean 1234.56
/// whichever locale is in use, and the formatting is what follows the locale.
/// More decimals than the currency has are reported rather than rounded away,
/// because silently swallowing an öre is worse than asking again.
MoneyInput parseMoneyInput(const QString& text, domain::Currency currency);

/// A string that has been read as an interest rate.
struct PercentageInput {
    bool ok = false;
    domain::Percentage value{};
    QString error;
    QString label;
};

/// Reads a rate in percent with the same rules as money: "4,5" is 4.50 %,
/// stored as basis points.
PercentageInput parsePercentageInput(const QString& text);

/// A string that has been read as a loan interest rate.
struct InterestRateInput {
    bool ok = false;
    domain::InterestRate value{};
    QString error;
    QString label;
};

/// Reads a loan rate in percent with the same rules as money, keeping three
/// decimals: "2,655" is 2.655 %, stored as parts per 100 000. A fourth decimal
/// is refused rather than rounded away.
InterestRateInput parseInterestRateInput(const QString& text);

/// A string that has been read as a loan term in years.
struct TermYearsInput {
    bool ok = false;
    /// The term in whole months, rounded to the nearest month.
    int months = 0;
    QString error;
};

/// Reads the years left on a loan, with the same separator rules as money, and
/// turns them into the whole months the model stores. "18,5" is 222 months.
TermYearsInput parseTermYearsInput(const QString& text);

/// The months a loan has left as whole years for a field: "18" or "18,5".
/// Zero months, which means the term was never stated, formats as an empty
/// string so a field with nothing recorded reopens empty.
QString formatTermYears(int months);

} // namespace hinkwise::presentation
