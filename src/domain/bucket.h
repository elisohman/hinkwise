// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "percentage.h"
#include "range.h"

#include <array>
#include <cstdint>
#include <optional>

namespace hinkwise::domain {

/// The four buckets of the model. Order matters: it is the order in which the
/// model says you fill them, and the order used in every UI listing.
enum class Bucket : std::uint8_t {
    Buffer = 0,
    Preserve = 1,
    Growth = 2,
    Play = 3,
};

inline constexpr std::array<Bucket, 4> allBuckets()
{
    return {Bucket::Buffer, Bucket::Preserve, Bucket::Growth, Bucket::Play};
}

/// A spending horizon in years. `maxYears` is empty for "10 years or more";
/// Play has no horizon at all, expressed by `Horizon::any()`.
struct Horizon {
    int minYears = 0;
    std::optional<int> maxYears = 0;

    static constexpr Horizon any() { return Horizon{0, std::nullopt}; }

    bool hasUpperBound() const { return maxYears.has_value(); }
};

/// The static facts about each bucket, transcribed from the model.
///
/// These numbers are the substance of the application: if they drift, the app
/// stops being an implementation of the four-bucket model and becomes a generic
/// budget tool with four labels. They are asserted against the source in
/// tests/domain/tst_bucket.cpp.
struct BucketFacts {
    const char* label;
    const char* purpose;
    Horizon horizon;
    PercentageRange nominalReturn;
    PercentageRange realReturn;
    Range<int> riskBand; ///< KIID / SRRI scale, 1-7; Play spans the whole scale
    bool defensive;      ///< buffer and preserve defend; growth and play attack
};

const BucketFacts& factsOf(Bucket bucket);
const char* labelOf(Bucket bucket);

/// True for buckets whose money is expected to be spent within the next few
/// years. Used to separate "defence" from "engine" in the dashboard.
bool isNearTerm(Bucket bucket);

} // namespace hinkwise::domain
