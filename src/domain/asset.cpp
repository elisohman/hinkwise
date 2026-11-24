// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "asset.h"

#include <array>
#include <stdexcept>
#include <utility>

namespace hinkwise::domain {

namespace {

using enum AssetKind;
using enum Bucket;

// Transcribed from the model: cash cushions shocks, funds and occupational
// pension build wealth, single shares and crypto are the play money, and
// housing plus every pension promise sits in preserve value because they are
// not spendable on demand.
constexpr std::array<AssetKindFacts, 10> kFacts{{
    {"Bank account", Buffer, true},
    {"Fund (index or managed)", Growth, true},
    {"Individual shares", Play, true},
    {"Crypto", Play, true},
    {"Real estate (market value)", Preserve, false},
    {"Pension income", Preserve, false},
    {"Occupational pension", Growth, false},
    {"Premium pension", Growth, false},
    {"Traditional life insurance", Preserve, false},
    {"Other", Preserve, true},
}};

} // namespace

const AssetKindFacts& factsOf(AssetKind kind)
{
    return kFacts[static_cast<std::size_t>(kind)];
}

Asset makeAsset(std::string id, std::string name, AssetKind kind)
{
    if (id.empty())
        throw std::invalid_argument{"makeAsset: id must not be empty"};
    if (name.empty())
        throw std::invalid_argument{"makeAsset: name must not be empty"};

    const AssetKindFacts& facts = factsOf(kind);
    Asset asset;
    asset.id = std::move(id);
    asset.name = std::move(name);
    asset.kind = kind;
    asset.bucket = facts.defaultBucket;
    asset.liquid = facts.defaultLiquid;
    return asset;
}

Asset withBucket(Asset asset, Bucket bucket)
{
    asset.bucket = bucket;
    return asset;
}

} // namespace hinkwise::domain
