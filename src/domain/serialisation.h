// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "asset.h"
#include "bucket.h"
#include "liability.h"
#include "money.h"
#include "phase.h"
#include "profile.h"

#include <optional>
#include <string_view>

namespace hinkwise::domain {

/// The text form of the domain's enums, as written to the database and to the
/// JSON backup.
///
/// These keys are a file format, not a display string: a backup written today
/// has to load next year, so they are spelled out here, kebab-case for the
/// multi-word names and upper case for the currency codes. They are deliberately
/// independent of labelOf(), which is free to change with the interface.
///
/// There is no throwing overload on purpose. Unrecognised text is a corrupt or
/// foreign file, which is a condition the caller must report to the user, so it
/// comes back as std::nullopt.
const char* storageKey(Phase phase);
std::optional<Phase> phaseFromStorageKey(std::string_view key);

/// "SEK", "EUR", "USD" - the ISO code, so there is one spelling per currency.
const char* storageKey(Currency currency);
std::optional<Currency> currencyFromStorageKey(std::string_view key);

/// "bank-account", "fund", ... in AssetKind order.
const char* storageKey(AssetKind kind);
std::optional<AssetKind> assetKindFromStorageKey(std::string_view key);

/// "mortgage", "student-loan", ... in LiabilityKind order.
const char* storageKey(LiabilityKind kind);
std::optional<LiabilityKind> liabilityKindFromStorageKey(std::string_view key);

/// "buffer", "preserve", "growth", "play".
const char* storageKey(Bucket bucket);
std::optional<Bucket> bucketFromStorageKey(std::string_view key);

/// "accounting", "planning". The mode decides how old input is talked about,
/// never what is stored, so the two keys describe a reading of the data rather
/// than a different data model.
const char* storageKey(TrackingMode mode);
std::optional<TrackingMode> trackingModeFromStorageKey(std::string_view key);

} // namespace hinkwise::domain
