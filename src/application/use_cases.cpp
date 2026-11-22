// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "use_cases.h"

#include "domain/jurisdiction.h"
#include "domain/percentage.h"

#include <string>
#include <utility>

namespace hinkwise::application {

Status recordMonth(IMonthFlowRepository& flows, domain::YearMonth month,
                   const domain::MonthFlow& flow)
{
    if (flow.income.isNegative())
        return Status::err("Income cannot be negative.");
    if (flow.needs.isNegative())
        return Status::err("Needs cannot be negative.");
    if (flow.wants.isNegative())
        return Status::err("Wants cannot be negative.");
    if (flow.saving.isNegative())
        return Status::err("Saving cannot be negative.");

    return flows.upsert(month, flow);
}

Status recordValuation(IValuationRepository& valuations, std::string_view assetId,
                       domain::YearMonth month, domain::Money balance)
{
    if (assetId.empty())
        return Status::err("A valuation needs an asset.");
    if (balance.isNegative())
        return Status::err("A valuation cannot be negative.");

    return valuations.upsert(assetId, month, balance);
}

Status addAsset(IAssetRepository& assets, const domain::Asset& asset)
{
    if (asset.id.empty() || asset.name.empty())
        return Status::err("An asset needs an id and a name.");

    return assets.upsert(asset);
}

Status removeAsset(IAssetRepository& assets, std::string_view assetId)
{
    if (assetId.empty())
        return Status::err("An asset id is needed to remove it.");

    return assets.remove(assetId);
}

Status addLiability(ILiabilityRepository& liabilities, const domain::Liability& liability)
{
    if (liability.id.empty() || liability.name.empty())
        return Status::err("A liability needs an id and a name.");
    if (liability.interestRate.isNegative())
        return Status::err("A liability's interest rate cannot be negative.");

    return liabilities.upsert(liability);
}

Status removeLiability(ILiabilityRepository& liabilities, std::string_view liabilityId)
{
    if (liabilityId.empty())
        return Status::err("A liability id is needed to remove it.");

    return liabilities.remove(liabilityId);
}

Result<domain::Profile> createProfile(const std::string& displayName, domain::Currency currency,
                                      domain::Phase phase)
{
    if (displayName.empty())
        return Result<domain::Profile>::err("A household needs a name.");

    domain::Profile profile;
    profile.displayName = displayName;
    profile.baseCurrency = currency;
    profile.phase = phase;
    profile.riskTolerance = domain::Percentage::fromBasisPoints(5000);
    profile.jurisdiction = domain::swedishJurisdiction();

    return Result<domain::Profile>::ok(std::move(profile));
}

Status changePhase(IProfileRepository& profiles, IPhaseHistoryRepository& history,
                   domain::Profile& profile, domain::Phase next, std::string_view isoTimestamp)
{
    if (isoTimestamp.empty())
        return Status::err("Changing phase needs a timestamp.");
    if (next == profile.phase)
        return Status::err("The household is already in that phase.");

    const Status appended = history.append(PhaseChange{std::string{isoTimestamp}, next});
    if (!appended.ok())
        return appended;

    domain::Profile updated = profile;
    updated.phase = next;
    const Status saved = profiles.save(updated);
    if (!saved.ok())
        return saved;

    profile.phase = next;
    return Status::success();
}

} // namespace hinkwise::application
