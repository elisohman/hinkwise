// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "examples.h"

#include "domain/asset.h"
#include "domain/household.h"
#include "domain/interest_rate.h"
#include "domain/jurisdiction.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "use_cases.h"

#include <string>
#include <utility>
#include <vector>

namespace hinkwise::application {

namespace {

/// Whole kronor as the stories are told: "80 000" is 80 000 kr, and the minor
/// units are filled in here rather than at every call.
domain::Money kronor(std::int64_t whole)
{
    return domain::Money{whole * 100, domain::Currency::Sek};
}

/// The ids of one seed. The profile id is in every id because asset and
/// liability ids are unique across the whole file: two copies of the same
/// example must not collide.
std::string idFor(std::int64_t profileId, const std::string& suffix)
{
    return "example-" + std::to_string(profileId) + "-" + suffix;
}

/// One holding of the story and what it is worth this month.
struct Holding {
    domain::Asset asset;
    domain::Money balance;
};

/// One debt of the story and what is still owed this month.
struct Loan {
    domain::Liability liability;
    domain::Money balance;
};

/// A month of the example's cashflow. Every recorded month is the same, which
/// is what makes the example a standing budget rather than a story about one
/// unusual spring.
domain::MonthFlow monthlyFlow(const domain::Money& income, const domain::Money& needs,
                              const domain::Money& wants, const domain::Money& saving,
                              const domain::Money& toBuffer, const domain::Money& toInvesting,
                              const domain::Money& toAmortisation)
{
    domain::MonthFlow flow;
    flow.income = income;
    flow.needs = needs;
    flow.wants = wants;
    flow.saving = saving;
    flow.savingSplit = domain::SavingsBreakdown{toBuffer, toInvesting, toAmortisation};
    return flow;
}

} // namespace

domain::Profile exampleProfile(ExampleProfile kind)
{
    domain::Profile profile;
    profile.baseCurrency = domain::Currency::Sek;
    profile.jurisdiction = domain::swedishJurisdiction();
    profile.riskTolerance = domain::Percentage::fromBasisPoints(5000);
    profile.isExample = true;

    // The examples record every month - twelve of them are in the seed - so
    // accounting is the mode that reads their history the way it was written.
    profile.trackingMode = domain::TrackingMode::Accounting;

    switch (kind) {
    case ExampleProfile::BuildingWealth:
        profile.displayName = "Example: building wealth";
        profile.phase = domain::Phase::BuildingWealth;
        break;
    case ExampleProfile::Preserving:
        profile.displayName = "Example: preserving";
        profile.phase = domain::Phase::Preserving;
        break;
    }
    return profile;
}

Status seedExample(ExampleProfile kind, std::int64_t profileId, IAssetRepository& assets,
                   ILiabilityRepository& liabilities, IMonthFlowRepository& flows,
                   IValuationRepository& valuations, IDebtBalanceRepository& debtBalances,
                   domain::YearMonth currentMonth)
{
    std::vector<Holding> holdings;
    std::vector<Loan> loans;
    domain::MonthFlow flow;

    const auto addHolding = [&holdings, profileId](const std::string& suffix,
                                                   const std::string& name, domain::AssetKind kind,
                                                   domain::Money balance) {
        holdings.push_back(
            Holding{domain::makeAsset(idFor(profileId, suffix), name, kind), std::move(balance)});
    };

    switch (kind) {
    case ExampleProfile::BuildingWealth: {
        // 30 000 kr in, 15 000 needs and 9 000 wants, so 6 000 is saved: 20 %
        // of income, which is the model's own 50/30/20 split. The 6 000 is
        // divided the way the story repays its mortgage - 2 000 to the buffer,
        // 1 000 to investing and the 3 000 amortisation the loan actually
        // charges - so the breakdown and the loan terms agree.
        flow = monthlyFlow(kronor(30000), kronor(15000), kronor(9000), kronor(6000), kronor(2000),
                           kronor(1000), kronor(3000));

        // The buffer is 80 000 against 24 000 kr of monthly expenses: three and
        // a third months, inside the 0.5-6 month band, so nothing complains.
        addHolding("buffer", "Buffer account", domain::AssetKind::BankAccount, kronor(80000));
        addHolding("index-funds", "Index funds", domain::AssetKind::Fund, kronor(350000));
        addHolding("flat", "Flat", domain::AssetKind::RealEstate, kronor(2200000));
        addHolding("income-pension", "Income pension", domain::AssetKind::PensionIncome,
                   kronor(900000));
        // 25 000 of play against 350 000 of growth is under the model's 10 %
        // cap, which is the point of showing it at all.
        addHolding("play", "Play money", domain::AssetKind::Equity, kronor(25000));

        // The flat is worth more than the loan, so the equity is positive and
        // the mortgage is not mistaken for the household's only entry.
        loans.push_back(Loan{
            domain::makeLiability(idFor(profileId, "mortgage"), "Mortgage",
                                  domain::LiabilityKind::Mortgage,
                                  domain::InterestRate::fromPercent(2.650),
                                  domain::RepaymentStyle::FixedAmortisation, kronor(3000)),
            kronor(1800000),
        });
        break;
    }
    case ExampleProfile::Preserving: {
        // The same household later: 24 000 kr a month of living costs, covered
        // by 27 000 kr of pension income, with 3 000 kr left over. Saving 3 000
        // of 27 000 is 11 %, so the model's tenth-of-income advice stays quiet.
        flow = monthlyFlow(kronor(27000), kronor(14000), kronor(10000), kronor(3000), kronor(2000),
                           kronor(1000), kronor(0));

        // 900 000 against 288 000 kr of annual expenses is just over three
        // years, inside the preserving buffer's 1-5 year band.
        addHolding("buffer", "Buffer account", domain::AssetKind::BankAccount, kronor(900000));
        addHolding("index-funds", "Index funds", domain::AssetKind::Fund, kronor(2000000));
        addHolding("flat", "Flat", domain::AssetKind::RealEstate, kronor(2200000));
        addHolding("income-pension", "Income pension", domain::AssetKind::PensionIncome,
                   kronor(5000000));
        addHolding("play", "Play money", domain::AssetKind::Equity, kronor(25000));

        // Flat and pension add up to 7 200 000, which is exactly the low end of
        // preserve's 25-35 years of expenses: funded, not below its band.
        // There is no mortgage left, which is why there is no loan here.
        break;
    }
    }

    for (const Holding& holding : holdings) {
        const Status stored = addAsset(assets, holding.asset);
        if (!stored.ok())
            return stored;
        const Status valued =
            recordValuation(valuations, holding.asset.id, currentMonth, holding.balance);
        if (!valued.ok())
            return valued;
    }

    for (const Loan& loan : loans) {
        const Status stored = addLiability(liabilities, loan.liability);
        if (!stored.ok())
            return stored;
        const Status balanced =
            recordDebtBalance(debtBalances, loan.liability.id, currentMonth, loan.balance);
        if (!balanced.ok())
            return balanced;
    }

    // Twelve months ending with this one, so the cashflow history is a year and
    // the newest month is the one the household would update today.
    for (int monthsAgo = 11; monthsAgo >= 0; --monthsAgo) {
        const Status recorded = recordMonth(flows, currentMonth.plusMonths(-monthsAgo), flow);
        if (!recorded.ok())
            return recorded;
    }

    return Status::success();
}

} // namespace hinkwise::application
