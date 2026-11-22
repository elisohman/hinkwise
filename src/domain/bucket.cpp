// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bucket.h"

#include <array>

namespace hinkwise::domain {

namespace {

using enum Bucket;

// Transcribed from the four-bucket model's summary table.
//
// Play has no expected return and no bounded risk: that is the point of it.
// Encoding "no expectation" as 0 % real and 1-7 risk would be a lie, so those
// two entries are marked by the label and by the whole risk scale, and the UI
// renders them as an em dash.
constexpr std::array<BucketFacts, 4> kFacts{{
    {
        "Buffer",
        "Absorb shocks, seize opportunities, and fund spending within three years.",
        Horizon{0, 3},
        {Percentage::fromBasisPoints(0), Percentage::fromBasisPoints(200)},
        {Percentage::fromBasisPoints(-100), Percentage::fromBasisPoints(0)},
        {1, 2},
        true,
    },
    {
        "Preserve value",
        "Keep pace with inflation, smooth out market swings, and refill the buffer.",
        Horizon{3, 9},
        {Percentage::fromBasisPoints(300), Percentage::fromBasisPoints(600)},
        {Percentage::fromBasisPoints(200), Percentage::fromBasisPoints(300)},
        {3, 4},
        true,
    },
    {
        "Growth",
        "Make the money you will live on later. The engine of the economy.",
        Horizon{10, std::nullopt},
        {Percentage::fromBasisPoints(600), Percentage::fromBasisPoints(900)},
        {Percentage::fromBasisPoints(500), Percentage::fromBasisPoints(700)},
        {5, 6},
        false,
    },
    {
        "Play",
        "Give the emotional part of investing somewhere to go, so the other buckets stay "
        "untouched.",
        Horizon::any(),
        {Percentage::fromBasisPoints(0), Percentage::fromBasisPoints(0)},
        {Percentage::fromBasisPoints(0), Percentage::fromBasisPoints(0)},
        {1, 7},
        false,
    },
}};

} // namespace

const BucketFacts& factsOf(Bucket bucket)
{
    return kFacts[static_cast<std::size_t>(bucket)];
}

const char* labelOf(Bucket bucket)
{
    return factsOf(bucket).label;
}

bool isNearTerm(Bucket bucket)
{
    return bucket == Bucket::Buffer;
}

} // namespace hinkwise::domain
