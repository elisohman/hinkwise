// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serialisation.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace hinkwise::domain {

namespace {

template <typename Enum> struct StorageKey {
    const char* key;
    Enum value;
};

/// The tables are ordered by enum value, so the forward lookup is an index.
/// An out-of-range cast (a corrupt enum) yields an empty key rather than
/// undefined behaviour; it then fails to load, which is the honest outcome.
template <typename Enum, std::size_t N>
const char* keyOf(const std::array<StorageKey<Enum>, N>& entries, Enum value)
{
    const auto index = static_cast<std::size_t>(value);
    return index < N ? entries[index].key : "";
}

template <typename Enum, std::size_t N>
std::optional<Enum> valueOf(const std::array<StorageKey<Enum>, N>& entries, std::string_view key)
{
    for (const StorageKey<Enum>& entry : entries) {
        if (key == entry.key)
            return entry.value;
    }
    return std::nullopt;
}

constexpr std::array<StorageKey<Phase>, 4> kPhaseKeys{{
    {"getting-started", Phase::GettingStarted},
    {"building-wealth", Phase::BuildingWealth},
    {"preserving", Phase::Preserving},
    {"financially-independent", Phase::FinanciallyIndependent},
}};

constexpr std::array<StorageKey<AssetKind>, 10> kAssetKindKeys{{
    {"bank-account", AssetKind::BankAccount},
    {"fund", AssetKind::Fund},
    {"equity", AssetKind::Equity},
    {"crypto", AssetKind::Crypto},
    {"real-estate", AssetKind::RealEstate},
    {"pension-income", AssetKind::PensionIncome},
    {"pension-occupational", AssetKind::PensionOccupational},
    {"pension-premium", AssetKind::PensionPremium},
    {"traditional-life", AssetKind::TraditionalLife},
    {"other", AssetKind::Other},
}};

constexpr std::array<StorageKey<LiabilityKind>, 6> kLiabilityKindKeys{{
    {"mortgage", LiabilityKind::Mortgage},
    {"student-loan", LiabilityKind::StudentLoan},
    {"consumer-loan", LiabilityKind::ConsumerLoan},
    {"credit-card", LiabilityKind::CreditCard},
    {"car-loan", LiabilityKind::CarLoan},
    {"other", LiabilityKind::Other},
}};

constexpr std::array<StorageKey<Bucket>, 4> kBucketKeys{{
    {"buffer", Bucket::Buffer},
    {"preserve", Bucket::Preserve},
    {"growth", Bucket::Growth},
    {"play", Bucket::Play},
}};

constexpr std::array<StorageKey<TrackingMode>, 2> kTrackingModeKeys{{
    {"accounting", TrackingMode::Accounting},
    {"planning", TrackingMode::Planning},
}};

constexpr std::array kCurrencies{Currency::Sek, Currency::Eur, Currency::Usd};

} // namespace

const char* storageKey(Phase phase)
{
    return keyOf(kPhaseKeys, phase);
}

std::optional<Phase> phaseFromStorageKey(std::string_view key)
{
    return valueOf(kPhaseKeys, key);
}

const char* storageKey(Currency currency)
{
    return isoCodeOf(currency);
}

std::optional<Currency> currencyFromStorageKey(std::string_view key)
{
    for (Currency currency : kCurrencies) {
        if (key == isoCodeOf(currency))
            return currency;
    }
    return std::nullopt;
}

const char* storageKey(AssetKind kind)
{
    return keyOf(kAssetKindKeys, kind);
}

std::optional<AssetKind> assetKindFromStorageKey(std::string_view key)
{
    return valueOf(kAssetKindKeys, key);
}

const char* storageKey(LiabilityKind kind)
{
    return keyOf(kLiabilityKindKeys, kind);
}

std::optional<LiabilityKind> liabilityKindFromStorageKey(std::string_view key)
{
    return valueOf(kLiabilityKindKeys, key);
}

const char* storageKey(Bucket bucket)
{
    return keyOf(kBucketKeys, bucket);
}

std::optional<Bucket> bucketFromStorageKey(std::string_view key)
{
    return valueOf(kBucketKeys, key);
}

const char* storageKey(TrackingMode mode)
{
    return keyOf(kTrackingModeKeys, mode);
}

std::optional<TrackingMode> trackingModeFromStorageKey(std::string_view key)
{
    return valueOf(kTrackingModeKeys, key);
}

} // namespace hinkwise::domain
