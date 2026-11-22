// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "asset.h"
#include "bucket.h"
#include "liability.h"
#include "money.h"
#include "percentage.h"

#include <array>
#include <cstddef>
#include <span>

namespace hinkwise::domain {

/// An asset together with what it is worth today. The sheet points at the
/// asset instead of copying it, so a rename in the profile shows up here
/// without the sheet having to be rebuilt for that reason alone.
struct Holding {
    const Asset* asset = nullptr;
    Money balance{};
};

/// A debt together with what is left to repay.
struct Debt {
    const Liability* liability = nullptr;
    Money balance{};
};

/// One household's position, reduced to the figures every screen asks for.
///
/// Only aggregates are kept: the sheet is a computed snapshot, and the
/// holdings and debts it came from stay where they live.
struct BalanceSheet {
    Money totalAssets{};
    Money totalLiabilities{};
    Money netWorth{};
    Money liquidAssets{};
    /// Indexed by Bucket, in fill order.
    std::array<Money, 4> perBucket{};
    /// Indexed by Bucket. The same holdings as perBucket with housing and the
    /// pension kinds left out, because a projection that starts from a flat
    /// would measure a household as funded on money it cannot spend. Their sum
    /// is headlineAssets.
    std::array<Money, 4> investablePerBucket{};
    /// Assets that count in the cautious headline: everything except housing
    /// and the pension kinds, further filtered by Asset::includedInHeadline.
    /// Kept as a figure because the sheet no longer holds the holdings.
    Money headlineAssets{};

    /// With housing and pension included this is simply netWorth. Without them
    /// it is the money the household can actually act on, minus every debt —
    /// which is why it can be negative while netWorth is comfortably positive.
    Money headlineNetWorth(bool includePensionAndHousing) const;

    /// This bucket's share of totalAssets, in basis points, truncated towards
    /// zero. Zero when there are no assets.
    Percentage bucketShare(Bucket bucket) const;
};

/// Sums the debts the model calls bad into one figure worth paying down.
Money badLoanTotal(std::span<const Debt> debts, Percentage mortgageBenchmark);

/// Builds the snapshot. All amounts must share one currency; the currency is
/// taken from the first holding (or the first debt when there are no
/// holdings). Mixing currencies throws std::invalid_argument, as Money does.
/// Empty input is allowed and yields zeroes in SEK.
/// Throws std::invalid_argument when a Holding or Debt has no asset/liability.
BalanceSheet buildBalanceSheet(std::span<const Holding> holdings, std::span<const Debt> debts);

} // namespace hinkwise::domain
