// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rules.h"

#include "bucket.h"
#include "phase.h"
#include "targets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hinkwise::domain {

namespace {

Money bucketBalance(const HouseholdFacts& facts, Bucket bucket)
{
    return facts.sheet.perBucket[static_cast<std::size_t>(bucket)];
}

const ResolvedTarget& targetOf(const HouseholdFacts& facts, Bucket bucket)
{
    return facts.targets[static_cast<std::size_t>(bucket)];
}

/// True when the bucket states a band and its balance sits below the low end.
bool isBelowBand(const HouseholdFacts& facts, Bucket bucket)
{
    const ResolvedTarget& target = targetOf(facts, bucket);
    return target.hasTarget() && bucketBalance(facts, bucket) < target.band.low;
}

/// True when the bucket states a band and its balance sits above the high end.
bool isAboveBand(const HouseholdFacts& facts, Bucket bucket)
{
    const ResolvedTarget& target = targetOf(facts, bucket);
    return target.hasTarget() && bucketBalance(facts, bucket) > target.band.high;
}

// ---------------------------------------------------------------------------
// The rules, one class each.
//
// Every rule is a pure function of the facts and the metrics: no statics, no
// ordering assumptions, no knowledge of the rules around it. The coaching tone
// lives in the text; the trigger lives in evaluate(). A rule that does not
// apply returns nullopt rather than an Insight with a quiet severity, so the
// list a screen shows only ever contains things worth saying.
// ---------------------------------------------------------------------------

class ExpensesMissingRule final : public Rule {
public:
    static constexpr std::string_view idText{"expenses-missing"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts&, const Metrics& metrics) const override
    {
        if (!metrics.annualExpenses.isZero())
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Critical,
            .title = "Nothing can be sized until expenses are recorded",
            .why =
                "Every target in the model is a multiple of what the household spends, so with no "
                "expenses there is nothing to size the buckets against.",
            .suggestion = "Record monthly needs and wants, and every target and check below will "
                          "resolve.",
        };
    }
};

class BufferEmptyRule final : public Rule {
public:
    static constexpr std::string_view idText{"buffer-empty"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        if (!bucketBalance(facts, Bucket::Buffer).isZero())
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Critical,
            .title = "The buffer is empty",
            .why =
                "The model always funds the buffer first: without it an unexpected bill is paid "
                "with borrowed money or by selling investments at whatever price the day offers.",
            .suggestion = "Move the next free krona into the buffer before any other bucket.",
        };
    }
};

class BufferUnderfundedRule final : public Rule {
public:
    static constexpr std::string_view idText{"buffer-underfunded"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        // An empty buffer is already reported as Critical; repeating it here as
        // a Warning is noise, not a second signal.
        if (bucketBalance(facts, Bucket::Buffer).isZero())
            return std::nullopt;

        if (!isBelowBand(facts, Bucket::Buffer))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Warning,
            .title = "The buffer is below its band",
            .why = "Money needed within three years must not sit in the market; the buffer is what "
                   "keeps a short-term need from forcing a sale of growth in a downturn.",
            .suggestion = "Fill the buffer to the low end of its band before adding to preserve, "
                          "growth or play.",
        };
    }
};

class BufferOverfullRule final : public Rule {
public:
    static constexpr std::string_view idText{"buffer-overfull"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        if (!isAccumulating(facts.phase) || !isAboveBand(facts, Bucket::Buffer))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Info,
            .title = "The buffer is above its band",
            .why = "A buffer is sized for money needed within three years; everything above the "
                   "band will not be needed that soon, and cash held longer than that loses to "
                   "inflation.",
            .suggestion = "Move the excess from the buffer to the growth bucket.",
        };
    }
};

class PlayOverCapRule final : public Rule {
public:
    static constexpr std::string_view idText{"play-over-cap"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        const ResolvedTarget& play = targetOf(facts, Bucket::Play);
        if (!play.hasCap() || !(bucketBalance(facts, Bucket::Play) > *play.cap))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Warning,
            .title = "Play is worth more than its cap allows",
            .why =
                "Ten percent of growth is the model's safe-to-fail limit: play money may be "
                "lost, and past that limit a loss starts to touch money the household counts on.",
            .suggestion = "Move the excess out of play, or grow growth until play is back within a "
                          "tenth of it.",
        };
    }
};

class BadLoanWhileBufferThinRule final : public Rule {
public:
    static constexpr std::string_view idText{"bad-loan-while-buffer-thin"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        if (!facts.badLoanTotal.isPositive() || !isBelowBand(facts, Bucket::Buffer))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Warning,
            .title = "An expensive loan is running beside a thin buffer",
            .why = "Consumer debt above a mortgage rate costs more every month than an unfunded "
                   "buffer earns, so the household pays for the protection twice.",
            .suggestion =
                "Repay the expensive loan first, then refill the buffer to the low end of "
                "its band.",
        };
    }
};

class SavingsRateLowRule final : public Rule {
public:
    static constexpr std::string_view idText{"savings-rate-low"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts,
                                    const Metrics& metrics) const override
    {
        if (!(metrics.savingsRate < Percentage::fromBasisPoints(1000)) ||
            !facts.flow.income.isPositive())
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Advice,
            .title = "Less than a tenth of income is being saved",
            .why = "The model says pay yourself first, 10-20 % of income including amortisation; "
                   "below that the buckets fill too slowly for any of them to change the outcome.",
            .suggestion = "Raise the monthly saving towards 10 %, counting amortisation, and let "
                          "the buffer take the first share.",
        };
    }
};

class NeedsShareHighRule final : public Rule {
public:
    static constexpr std::string_view idText{"needs-share-high"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts&, const Metrics& metrics) const override
    {
        if (!(Percentage::fromBasisPoints(5500) < metrics.needsShare))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Info,
            .title = "Needs take more than half of income",
            .why =
                "The model's 50/30/20 split gives needs half of income; when they take more, the "
                "room for wants and saving has to come from somewhere, which is a budget fact "
                "rather than a judgement.",
            .suggestion = "Check whether a recurring need can be renegotiated or moved before "
                          "trimming wants or saving.",
        };
    }
};

class PreserveThinWhenPreservingRule final : public Rule {
public:
    static constexpr std::string_view idText{"preserve-thin-when-preserving"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        const bool preserving =
            facts.phase == Phase::Preserving || facts.phase == Phase::FinanciallyIndependent;
        if (!preserving || !isBelowBand(facts, Bucket::Preserve))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Warning,
            .title = "The preserve bucket is below its band",
            .why =
                "The 5 + 25 rule funds five years of withdrawals from preserve, so a downturn in "
                "growth never forces a sale to pay for living costs.",
            .suggestion =
                "Refill preserve from growth while growth is up, aiming for at least five "
                "years of expenses.",
        };
    }
};

class CrashImpactHighRule final : public Rule {
public:
    static constexpr std::string_view idText{"crash-impact-high"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts&, const Metrics& metrics) const override
    {
        if (!(Percentage::fromBasisPoints(2500) < metrics.crashImpact))
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Advice,
            .title = "A share crash would move net worth by more than a quarter",
            .why = "Once housing and pension are counted, a 50 % fall in the growth bucket should "
                   "barely move total net worth; if it moves it more than a quarter, the growth "
                   "bucket is carrying too much of what the household owns.",
            .suggestion = "Let new saving go to preserve until a crash would cost less than a "
                          "quarter of net worth.",
        };
    }
};

class PhaseMaybeBuildingRule final : public Rule {
public:
    static constexpr std::string_view idText{"phase-maybe-building"};

    std::string_view id() const override { return idText; }

    std::optional<Insight> evaluate(const HouseholdFacts& facts, const Metrics&) const override
    {
        const ResolvedTarget& buffer = targetOf(facts, Bucket::Buffer);
        const bool bufferIsFull = buffer.hasTarget() && buffer.band.low.isPositive() &&
                                  bucketBalance(facts, Bucket::Buffer) >= buffer.band.high;
        if (facts.phase != Phase::GettingStarted || !bufferIsFull)
            return std::nullopt;

        return Insight{
            .id = std::string{idText},
            .severity = Severity::Info,
            .title = "The buffer is full, so building wealth may describe this better",
            .why =
                "Getting started is the phase for filling the buffer first; once the buffer sits "
                "at the top of its band, the work left is in growth, which is what building "
                "wealth means.",
            .suggestion = "Switch the phase to building wealth when it matches what you are doing; "
                          "only the guidance changes.",
        };
    }
};

} // namespace

std::vector<Insight> evaluateRules(const HouseholdFacts& facts, const Metrics& metrics)
{
    static const ExpensesMissingRule expensesMissing;
    static const BufferEmptyRule bufferEmpty;
    static const BufferUnderfundedRule bufferUnderfunded;
    static const BufferOverfullRule bufferOverfull;
    static const PlayOverCapRule playOverCap;
    static const BadLoanWhileBufferThinRule badLoanWhileBufferThin;
    static const SavingsRateLowRule savingsRateLow;
    static const NeedsShareHighRule needsShareHigh;
    static const PreserveThinWhenPreservingRule preserveThinWhenPreserving;
    static const CrashImpactHighRule crashImpactHigh;
    static const PhaseMaybeBuildingRule phaseMaybeBuilding;

    const std::array<const Rule*, 11> rules{
        &expensesMissing, &bufferEmpty,        &bufferUnderfunded,
        &bufferOverfull,  &playOverCap,        &badLoanWhileBufferThin,
        &savingsRateLow,  &needsShareHigh,     &preserveThinWhenPreserving,
        &crashImpactHigh, &phaseMaybeBuilding,
    };

    std::vector<Insight> fired;
    fired.reserve(rules.size());
    for (const Rule* rule : rules) {
        if (std::optional<Insight> insight = rule->evaluate(facts, metrics))
            fired.push_back(std::move(*insight));
    }

    std::sort(fired.begin(), fired.end(), [](const Insight& left, const Insight& right) {
        if (left.severity != right.severity)
            return left.severity < right.severity;
        return left.id < right.id;
    });
    return fired;
}

} // namespace hinkwise::domain
