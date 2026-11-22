// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bucket.h"

#include <cstdint>
#include <string>

namespace hinkwise::domain {

/// What a holding is, in the categories the four-bucket model talks about.
///
/// The pension kinds are separate on purpose: the model treats the state
/// pension, the occupational pension, the premium pension and a traditional
/// life insurance differently from money the household can move itself.
enum class AssetKind : std::uint8_t {
    BankAccount,
    Fund,
    Equity,
    Crypto,
    RealEstate,
    PensionIncome,
    PensionOccupational,
    PensionPremium,
    TraditionalLife,
    Other,
};

/// Where a kind belongs and whether it can be spent quickly, per the model.
///
/// These are defaults for a new holding, not laws about it: the household may
/// place an asset in another bucket with withBucket().
struct AssetKindFacts {
    const char* label;
    Bucket defaultBucket;
    bool defaultLiquid;
};

const AssetKindFacts& factsOf(AssetKind kind);

struct Asset {
    std::string id;
    std::string name;
    AssetKind kind = AssetKind::Other;
    Bucket bucket = Bucket::Preserve;
    /// Spendable at short notice, which is what makes it buffer material.
    bool liquid = true;
    /// Whether the household wants this asset in the cautious headline figure.
    /// Housing and pension kinds are excluded from that figure regardless.
    bool includedInHeadline = true;
};

/// A new holding with the kind's default bucket and liquidity.
/// Throws std::invalid_argument when the id or the name is empty.
Asset makeAsset(std::string id, std::string name, AssetKind kind);

/// The same asset placed in a different bucket, for when the household
/// disagrees with the default (a flat used as an investment, say).
Asset withBucket(Asset asset, Bucket bucket);

} // namespace hinkwise::domain
