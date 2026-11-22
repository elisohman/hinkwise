// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phase.h"

namespace hinkwise::domain {

namespace {

// ---------------------------------------------------------------------------
// The two sizing tables of the model.
//
// There are only two, because the model only states two: the accumulating
// phases share one set of sizes, and the preserving phases share another
// (the "5 + 25" rule). Getting started and building wealth differ in method,
// not in target; preserving and financially independent differ in tone, not in
// arithmetic.
// ---------------------------------------------------------------------------

BucketTargets buildingTargets()
{
    BucketTargets targets;

    // Buffer: 0.5 - 6 months of expenses, plus large one-off costs due within
    // three years.
    targets.byBucket[static_cast<std::size_t>(Bucket::Buffer)] = BucketTarget{
        BucketTarget::Kind::MultipleOfExpenses,
        {Percentage::fromBasisPoints(5000), Percentage::fromBasisPoints(60000)},
        ExpenseBasis::Monthly,
        true,
    };

    // Preserve: "as small as possible" in this phase; housing and pension
    // usually cover it already, so it is not a target to fill.
    targets.byBucket[static_cast<std::size_t>(Bucket::Preserve)] =
        BucketTarget{BucketTarget::Kind::NoTarget};

    // Growth: "as large as possible" until the goal is reached.
    targets.byBucket[static_cast<std::size_t>(Bucket::Growth)] =
        BucketTarget{BucketTarget::Kind::AsLargeAsPossible};

    // Play: a ceiling, 10 % of Growth, excluding pension savings.
    targets.byBucket[static_cast<std::size_t>(Bucket::Play)] = BucketTarget{
        BucketTarget::Kind::CapOfBucket,
        {Percentage::fromBasisPoints(0), Percentage::fromBasisPoints(1000)},
        ExpenseBasis::Monthly,
        false,
        Bucket::Growth,
    };

    return targets;
}

BucketTargets preservingTargets()
{
    BucketTargets targets;

    // Buffer: 3 +- 2 annual expenses, plus one-off costs within three years.
    // That is 1 to 5 years, with 3 as the middle.
    targets.byBucket[static_cast<std::size_t>(Bucket::Buffer)] = BucketTarget{
        BucketTarget::Kind::MultipleOfExpenses,
        {Percentage::fromBasisPoints(10000), Percentage::fromBasisPoints(50000)},
        ExpenseBasis::Annual,
        true,
    };

    // Preserve: 25 - 35 annual expenses, i.e. the 5 + 25 rule.
    targets.byBucket[static_cast<std::size_t>(Bucket::Preserve)] = BucketTarget{
        BucketTarget::Kind::MultipleOfExpenses,
        {Percentage::fromBasisPoints(250000), Percentage::fromBasisPoints(350000)},
        ExpenseBasis::Annual,
        false,
    };

    // Growth and Play become optional: "if you have won the game, stop playing".
    targets.byBucket[static_cast<std::size_t>(Bucket::Growth)] =
        BucketTarget{BucketTarget::Kind::NoTarget};
    targets.byBucket[static_cast<std::size_t>(Bucket::Play)] =
        BucketTarget{BucketTarget::Kind::NoTarget};

    return targets;
}

const BucketTargets& building()
{
    static const BucketTargets targets = buildingTargets();
    return targets;
}

const BucketTargets& preserving()
{
    static const BucketTargets targets = preservingTargets();
    return targets;
}

class GettingStartedPolicy final : public PhasePolicy {
public:
    Phase phase() const override { return Phase::GettingStarted; }
    const BucketTargets& targets() const override { return building(); }
    FlowDirection flow() const override { return FlowDirection::IntoBuckets; }
    HeadlineMetric headline() const override { return HeadlineMetric::YearsOfFreedom; }
};

class BuildingWealthPolicy final : public PhasePolicy {
public:
    Phase phase() const override { return Phase::BuildingWealth; }
    const BucketTargets& targets() const override { return building(); }
    FlowDirection flow() const override { return FlowDirection::IntoBuckets; }
    HeadlineMetric headline() const override { return HeadlineMetric::YearsOfFreedom; }
};

class PreservingPolicy final : public PhasePolicy {
public:
    Phase phase() const override { return Phase::Preserving; }
    const BucketTargets& targets() const override { return preserving(); }
    FlowDirection flow() const override { return FlowDirection::OutOfBuckets; }
    HeadlineMetric headline() const override { return HeadlineMetric::SafeWithdrawalRate; }
};

class FinanciallyIndependentPolicy final : public PhasePolicy {
public:
    Phase phase() const override { return Phase::FinanciallyIndependent; }
    const BucketTargets& targets() const override { return preserving(); }
    FlowDirection flow() const override { return FlowDirection::OutOfBuckets; }
    HeadlineMetric headline() const override { return HeadlineMetric::SafeWithdrawalRate; }
};

} // namespace

const PhasePolicy& policyFor(Phase phase)
{
    static const GettingStartedPolicy gettingStarted;
    static const BuildingWealthPolicy buildingWealth;
    static const PreservingPolicy preservingPhase;
    static const FinanciallyIndependentPolicy financiallyIndependent;

    switch (phase) {
    case Phase::GettingStarted:
        return gettingStarted;
    case Phase::BuildingWealth:
        return buildingWealth;
    case Phase::Preserving:
        return preservingPhase;
    case Phase::FinanciallyIndependent:
        return financiallyIndependent;
    }
    return buildingWealth;
}

const char* labelOf(Phase phase)
{
    switch (phase) {
    case Phase::GettingStarted:
        return "Getting started";
    case Phase::BuildingWealth:
        return "Building wealth";
    case Phase::Preserving:
        return "Preserving";
    case Phase::FinanciallyIndependent:
        return "Financially independent";
    }
    return "Unknown";
}

bool isAccumulating(Phase phase)
{
    return policyFor(phase).flow() == FlowDirection::IntoBuckets;
}

} // namespace hinkwise::domain
