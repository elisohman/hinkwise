// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "balance_sheet.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace hinkwise::domain {

namespace {

/// Housing and pension promises are wealth, but not wealth the household can
/// decide to spend this year. The cautious headline figure leaves them out.
bool isHousingOrPension(AssetKind kind)
{
    switch (kind) {
    case AssetKind::RealEstate:
    case AssetKind::PensionIncome:
    case AssetKind::PensionOccupational:
    case AssetKind::PensionPremium:
    case AssetKind::TraditionalLife:
        return true;
    case AssetKind::BankAccount:
    case AssetKind::Fund:
    case AssetKind::Equity:
    case AssetKind::Crypto:
    case AssetKind::Other:
        return false;
    }
    return false;
}

/// The currency everything has to be in. A household has one base currency,
/// so the first amount states it for the whole sheet; with no amounts at all
/// the sheet is zero SEK.
Currency currencyOf(std::span<const Holding> holdings, std::span<const Debt> debts)
{
    if (!holdings.empty())
        return holdings.front().balance.currency();
    if (!debts.empty())
        return debts.front().balance.currency();
    return Currency::Sek;
}

} // namespace

Money BalanceSheet::headlineNetWorth(bool includePensionAndHousing) const
{
    return includePensionAndHousing ? netWorth : headlineAssets - totalLiabilities;
}

Percentage BalanceSheet::bucketShare(Bucket bucket) const
{
    if (totalAssets.isZero())
        return Percentage::fromBasisPoints(0);

    const std::int64_t amount = perBucket[static_cast<std::size_t>(bucket)].minorUnits();
    const std::int64_t share = (amount * 10000) / totalAssets.minorUnits();
    return Percentage::fromBasisPoints(static_cast<std::int32_t>(share));
}

Money badLoanTotal(std::span<const Debt> debts, Percentage mortgageBenchmark)
{
    Money total =
        debts.empty() ? Money::zero(Currency::Sek) : Money::zero(debts.front().balance.currency());

    for (const Debt& debt : debts) {
        if (debt.liability == nullptr)
            throw std::invalid_argument{"badLoanTotal: debt without a liability"};
        if (isBadLoan(*debt.liability, mortgageBenchmark))
            total += debt.balance;
    }

    return total;
}

BalanceSheet buildBalanceSheet(std::span<const Holding> holdings, std::span<const Debt> debts)
{
    const Currency currency = currencyOf(holdings, debts);

    BalanceSheet sheet;
    sheet.totalAssets = Money::zero(currency);
    sheet.totalLiabilities = Money::zero(currency);
    sheet.liquidAssets = Money::zero(currency);
    sheet.headlineAssets = Money::zero(currency);
    for (const Bucket bucket : allBuckets())
        sheet.perBucket[static_cast<std::size_t>(bucket)] = Money::zero(currency);

    for (const Holding& holding : holdings) {
        if (holding.asset == nullptr)
            throw std::invalid_argument{"buildBalanceSheet: holding without an asset"};

        const Asset& asset = *holding.asset;
        sheet.totalAssets += holding.balance;
        sheet.perBucket[static_cast<std::size_t>(asset.bucket)] += holding.balance;
        if (asset.liquid)
            sheet.liquidAssets += holding.balance;
        if (asset.includedInHeadline && !isHousingOrPension(asset.kind))
            sheet.headlineAssets += holding.balance;
    }

    for (const Debt& debt : debts) {
        if (debt.liability == nullptr)
            throw std::invalid_argument{"buildBalanceSheet: debt without a liability"};
        sheet.totalLiabilities += debt.balance;
    }

    sheet.netWorth = sheet.totalAssets - sheet.totalLiabilities;
    return sheet;
}

} // namespace hinkwise::domain
