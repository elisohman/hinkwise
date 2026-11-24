// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/asset.h"
#include "domain/household.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "ports.h"
#include "result.h"

#include <string>
#include <string_view>

namespace hinkwise::application {

/// One use case per thing a screen can ask the household's data to do.
///
/// Every function validates the request and returns a message a person can
/// read when it is wrong. Nothing here throws and nothing decides policy: the
/// domain owns what a phase or a valuation means, and these functions only
/// check the input before handing it to a repository.

/// Records one month's income and spending. Negative amounts are rejected:
/// a flow describes money coming in and going out, not arithmetic offsets.
Status recordMonth(IMonthFlowRepository& flows, domain::YearMonth month,
                   const domain::MonthFlow& flow);

/// Records what one asset is worth in one month. A valuation cannot be
/// negative, and an asset id is required.
Status recordValuation(IValuationRepository& valuations, std::string_view assetId,
                       domain::YearMonth month, domain::Money balance);

/// Records what one debt still owes in one month, which is how a mortgage is
/// updated as it is paid down. The liability id is required and the amount
/// cannot be negative; recording the same month twice corrects it rather than
/// adding a second entry.
Status recordDebtBalance(IDebtBalanceRepository& debtBalances, std::string_view liabilityId,
                         domain::YearMonth month, domain::Money balance);

/// Adds an asset or replaces the one with the same id.
Status addAsset(IAssetRepository& assets, const domain::Asset& asset);

/// Removes an asset and everything recorded about it.
Status removeAsset(IAssetRepository& assets, std::string_view assetId);

/// Adds a liability or replaces the one with the same id.
Status addLiability(ILiabilityRepository& liabilities, const domain::Liability& liability);

/// Changes a liability's own terms - its rate, style, payment and fee - while
/// keeping its id, and therefore every balance recorded against it. A rate is
/// not a reason to delete a loan and lose its history: the history is what says
/// what has actually been repaid.
Status updateLiability(ILiabilityRepository& liabilities, const domain::Liability& liability);

/// Removes a liability and everything recorded about it.
Status removeLiability(ILiabilityRepository& liabilities, std::string_view liabilityId);

/// A new household, before it has been saved anywhere.
///
/// The jurisdiction starts Swedish because that is the model's home, and the
/// risk tolerance at the middle of every band. Both are editable later; the
/// point is that a new profile is complete rather than half-filled.
Result<domain::Profile> createProfile(const std::string& displayName, domain::Currency currency,
                                      domain::Phase phase);

/// Moves the household to `next`, appends the change to the history, and saves
/// the profile.
///
/// `isoTimestamp` is stamped by the caller, because the clock is not the
/// application layer's business. The change is validated first: an empty
/// timestamp or the phase the household already has is rejected, and the
/// profile is only mutated once both the history entry and the save succeed.
Status changePhase(IProfileRepository& profiles, IPhaseHistoryRepository& history,
                   domain::Profile& profile, domain::Phase next, std::string_view isoTimestamp);

} // namespace hinkwise::application
