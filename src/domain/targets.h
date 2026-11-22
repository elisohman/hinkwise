// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bucket.h"
#include "money.h"
#include "percentage.h"
#include "range.h"

#include <array>

namespace hinkwise::domain {

enum class ExpenseBasis {
    Monthly,
    Annual,
};

/// One bucket's sizing rule, as stated by the model rather than as a number.
///
/// Keeping the rule symbolic matters: "0.5-6 months of expenses" is true for
/// every household, while "41 000 kr" is true for one. The rule is resolved
/// into money only when expenses are known.
struct BucketTarget {
    enum class Kind {
        MultipleOfExpenses, ///< a band relative to monthly or annual expenses
        CapOfBucket,        ///< a ceiling expressed as a share of another bucket
        AsLargeAsPossible,  ///< deliberately open-ended
        NoTarget,           ///< optional, or covered by housing and pension
    };

    Kind kind = Kind::NoTarget;
    PercentageRange multiple{};
    ExpenseBasis basis = ExpenseBasis::Monthly;
    bool includesLumpSums = false;
    Bucket capBucket = Bucket::Growth;
};

struct BucketTargets {
    std::array<BucketTarget, 4> byBucket{};

    const BucketTarget& operator[](Bucket bucket) const
    {
        return byBucket[static_cast<std::size_t>(bucket)];
    }
};

struct TargetInputs {
    Money monthlyExpenses;
    Money annualExpenses;
    Money lumpSumsNextThreeYears;
    Money growthBalance;
    /// Where in the band the household wants to sit: 0 % cautious, 100 % bold.
    Percentage riskTolerance{Percentage::fromBasisPoints(5000)};
};

struct ResolvedTarget {
    BucketTarget::Kind kind = BucketTarget::Kind::NoTarget;
    MoneyRange band{};
    Money point{};

    bool hasBand() const
    {
        return kind == BucketTarget::Kind::MultipleOfExpenses ||
               kind == BucketTarget::Kind::CapOfBucket;
    }
};

using ResolvedTargets = std::array<ResolvedTarget, 4>;

/// Turns sizing rules into money for one household.
ResolvedTargets resolve(const BucketTargets& targets, const TargetInputs& inputs);

} // namespace hinkwise::domain
