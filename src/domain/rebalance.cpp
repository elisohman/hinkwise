// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rebalance.h"

#include <cstddef>

namespace hinkwise::domain {

namespace {

Money balanceOf(const RebalanceInput& input, Bucket bucket)
{
    return input.balances[static_cast<std::size_t>(bucket)];
}

const ResolvedTarget& targetOf(const RebalanceInput& input, Bucket bucket)
{
    return input.targets[static_cast<std::size_t>(bucket)];
}

/// True when the bucket states a band and its balance sits below the low end.
bool isBelowLow(const RebalanceInput& input, Bucket bucket)
{
    const ResolvedTarget& target = targetOf(input, bucket);
    return target.hasTarget() && balanceOf(input, bucket) < target.band.low;
}

/// True when the bucket states a band and its balance sits above the high end.
bool isAboveHigh(const RebalanceInput& input, Bucket bucket)
{
    const ResolvedTarget& target = targetOf(input, bucket);
    return target.hasTarget() && target.band.high < balanceOf(input, bucket);
}

Money shortfallToLow(const RebalanceInput& input, Bucket bucket)
{
    return targetOf(input, bucket).band.low - balanceOf(input, bucket);
}

Money excessAboveHigh(const RebalanceInput& input, Bucket bucket)
{
    return balanceOf(input, bucket) - targetOf(input, bucket).band.high;
}

Money smallerOf(Money left, Money right)
{
    return right < left ? right : left;
}

/// The plan's three promises, enforced in the one place every move passes
/// through: a real amount, two different buckets, and no sale out of Growth into
/// a falling market. The branches below already honour them; this is where they
/// stop being a habit of each branch and become a property of every plan.
void addTransfer(RebalanceProposal& proposal, const RebalanceInput& input, Bucket from, Bucket to,
                 Money amount)
{
    if (from == to || !amount.isPositive())
        return;
    if (input.marketDown && from == Bucket::Growth)
        return;

    proposal.transfers.push_back(Transfer{from, to, amount});
}

// ---------------------------------------------------------------------------
// Accumulating phases: the buckets fill, so the question is only which one gets
// the next krona. The order is the model's: buffer before everything, Play
// inside its ceiling, and a buffer that has overshot handed back to Growth.
// ---------------------------------------------------------------------------

RebalanceProposal planAccumulating(const RebalanceInput& input)
{
    RebalanceProposal proposal;

    const ResolvedTarget& play = targetOf(input, Bucket::Play);

    if (isBelowLow(input, Bucket::Buffer)) {
        proposal.nextContribution = Bucket::Buffer;
        proposal.rationale =
            "The buffer comes first: it is below its band, so every free krona goes "
            "there until it is back inside.";
        return proposal;
    }

    if (play.hasCap() && *play.cap < balanceOf(input, Bucket::Play)) {
        addTransfer(proposal, input, Bucket::Play, Bucket::Growth,
                    balanceOf(input, Bucket::Play) - *play.cap);
        proposal.nextContribution = Bucket::Growth;
        proposal.rationale =
            "Play is above its ceiling, so the excess moves to Growth and new money follows it.";
        return proposal;
    }

    if (isAboveHigh(input, Bucket::Buffer)) {
        const Money excess =
            balanceOf(input, Bucket::Buffer) - targetOf(input, Bucket::Buffer).point;
        // A point outside its own band can only come from a hand-built target;
        // with nothing above it there is nothing to move, so the plan falls
        // through to the ordinary case instead of describing a transfer it is
        // not making.
        if (excess.isPositive()) {
            addTransfer(proposal, input, Bucket::Buffer, Bucket::Growth, excess);
            proposal.nextContribution = Bucket::Growth;
            proposal.rationale =
                "The buffer is above its band, so everything above its target moves to Growth.";
            return proposal;
        }
    }

    proposal.nextContribution = Bucket::Growth;
    proposal.rationale =
        "Nothing is out of balance, so new money goes to Growth, the bucket that builds freedom.";
    return proposal;
}

// ---------------------------------------------------------------------------
// Preserving phases: the buckets are drawn down, so the buffer is refilled
// rather than filled and there is no free money left to allocate.
// ---------------------------------------------------------------------------

/// Preserve is the buffer's natural lender. Growth is the lender of last resort,
/// and only when it is not the thing that just fell.
RebalanceProposal refillBuffer(const RebalanceInput& input)
{
    RebalanceProposal proposal;

    const Money shortfall = shortfallToLow(input, Bucket::Buffer);
    const bool preserveIsLow = isBelowLow(input, Bucket::Preserve);

    if (preserveIsLow && !input.marketDown) {
        addTransfer(proposal, input, Bucket::Growth, Bucket::Buffer, shortfall);
        proposal.rationale =
            "Both the buffer and Preserve are below their bands, so Growth refills the buffer.";
        return proposal;
    }

    addTransfer(proposal, input, Bucket::Preserve, Bucket::Buffer, shortfall);
    if (preserveIsLow)
        proposal.rationale =
            "The market is down, so Growth is not sold into the fall: Preserve refills the buffer "
            "even though Preserve is below its band too.";
    else
        proposal.rationale = "The buffer is below its band, so Preserve refills it.";
    return proposal;
}

RebalanceProposal drainPreserveSurplus(const RebalanceInput& input)
{
    RebalanceProposal proposal;

    const ResolvedTarget& buffer = targetOf(input, Bucket::Buffer);
    Money remainder = excessAboveHigh(input, Bucket::Preserve);

    bool toppedUpBuffer = false;
    if (buffer.hasTarget() && balanceOf(input, Bucket::Buffer) < buffer.band.high) {
        const Money room = buffer.band.high - balanceOf(input, Bucket::Buffer);
        const Money fill = smallerOf(room, remainder);
        addTransfer(proposal, input, Bucket::Preserve, Bucket::Buffer, fill);
        toppedUpBuffer = fill.isPositive();
        remainder -= fill;
    }

    addTransfer(proposal, input, Bucket::Preserve, Bucket::Growth, remainder);

    if (!toppedUpBuffer)
        proposal.rationale = "Preserve is above its band, so the excess moves to Growth.";
    else if (remainder.isPositive())
        proposal.rationale =
            "Preserve is above its band: the excess tops up the buffer first and the rest goes to "
            "Growth.";
    else
        proposal.rationale =
            "Preserve is above its band, so the excess tops up the buffer and stops there.";

    return proposal;
}

RebalanceProposal planPreserving(const RebalanceInput& input)
{
    // No nextContribution in these phases: there is no free money to allocate by
    // design, and inventing one would quietly turn a drawdown into a build-up.
    if (isBelowLow(input, Bucket::Buffer))
        return refillBuffer(input);

    if (isAboveHigh(input, Bucket::Preserve))
        return drainPreserveSurplus(input);

    RebalanceProposal proposal;
    proposal.rationale = "Nothing needs moving this month.";
    return proposal;
}

} // namespace

RebalanceProposal planRebalance(const RebalanceInput& input)
{
    if (isAccumulating(input.phase))
        return planAccumulating(input);
    return planPreserving(input);
}

} // namespace hinkwise::domain
