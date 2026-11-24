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

#include <optional>
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
                              const domain::Money& wants, const domain::Money& saving)
{
    domain::MonthFlow flow;
    flow.income = income;
    flow.needs = needs;
    flow.wants = wants;
    flow.saving = saving;
    return flow;
}

/// The same month for a household that splits its saving: where the saved
/// money goes is part of the story in the accumulating phases.
domain::MonthFlow monthlyFlow(const domain::Money& income, const domain::Money& needs,
                              const domain::Money& wants, const domain::Money& saving,
                              const domain::Money& toBuffer, const domain::Money& toInvesting,
                              const domain::Money& toAmortisation)
{
    domain::MonthFlow flow = monthlyFlow(income, needs, wants, saving);
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

    switch (kind) {
    case ExampleProfile::GettingStarted:
        profile.displayName = "Example: getting started";
        profile.phase = domain::Phase::GettingStarted;
        // Twelve recorded months are a bookkeeping record, so accounting is
        // the mode that reads their history the way it was written.
        profile.trackingMode = domain::TrackingMode::Accounting;
        break;
    case ExampleProfile::BuildingWealth:
        profile.displayName = "Example: building wealth";
        profile.phase = domain::Phase::BuildingWealth;
        profile.trackingMode = domain::TrackingMode::Accounting;
        break;
    case ExampleProfile::Preserving:
        profile.displayName = "Example: preserving";
        profile.phase = domain::Phase::Preserving;
        // A household living off its capital sets a standing budget rather
        // than writing every month down, which is what planning means.
        profile.trackingMode = domain::TrackingMode::Planning;
        break;
    case ExampleProfile::FinanciallyIndependent:
        profile.displayName = "Example: financially independent";
        profile.phase = domain::Phase::FinanciallyIndependent;
        profile.trackingMode = domain::TrackingMode::Planning;
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

    // A holding normally sits in its kind's default bucket. The stories that
    // place a fund or a pension promise in preserve say so explicitly, because
    // that placement is what makes the bucket arithmetic in the story work.
    const auto addHolding = [&holdings,
                             profileId](const std::string& suffix, const std::string& name,
                                        domain::AssetKind kind, domain::Money balance,
                                        std::optional<domain::Bucket> bucket = std::nullopt) {
        domain::Asset asset = domain::makeAsset(idFor(profileId, suffix), name, kind);
        if (bucket.has_value())
            asset = domain::withBucket(std::move(asset), *bucket);
        holdings.push_back(Holding{std::move(asset), std::move(balance)});
    };

    switch (kind) {
    case ExampleProfile::GettingStarted: {
        // The article's 39 900 kr gross salary is 31 500 kr net, split 50/30/20:
        // 15 750 needs, 9 450 wants and 6 300 saved. The saving goes mostly to
        // the buffer - 3 300 - with the first 3 000 into growth, and nothing to
        // amortisation because the household has no debt.
        flow = monthlyFlow(kronor(31500), kronor(15750), kronor(9450), kronor(6300), kronor(3300),
                           kronor(3000), kronor(0));

        // 10 000 against 25 200 kr of monthly expenses is just under 0.4
        // months, below the accumulating band's 0.5-month floor. Filling the
        // buffer is the first task the model gives this household, so the
        // example shows the honest state rather than a finished one.
        addHolding("buffer", "Buffer account", domain::AssetKind::BankAccount, kronor(10000));
        addHolding("index-fund", "Index fund", domain::AssetKind::Fund, kronor(12000));
        // The occupational pension promise is preserve value in the story: it
        // is wealth the household cannot move, not an engine for growth.
        addHolding("occupational-pension", "Occupational pension",
                   domain::AssetKind::PensionOccupational, kronor(25000), domain::Bucket::Preserve);

        // No loans: a household getting started has no bad debt left to pay
        // down, which is the order the article puts after the buffer.
        break;
    }
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
        // The household lives on 24 000 kr a month of pension and spends all of
        // it: needs 14 000, wants 10 000, nothing saved. There is no saving to
        // split, because a household spending its capital is not saving.
        flow = monthlyFlow(kronor(24000), kronor(14000), kronor(10000), kronor(0));

        // 864 000 is three years of 288 000 kr of annual expenses: the middle
        // of the preserving buffer's 3 +- 2. Flat and index funds add up to
        // 7 200 000, exactly 25 annual expenses and the low end of preserve's
        // 25-35 band, so the model calls the preserve bucket funded.
        addHolding("buffer", "Buffer account", domain::AssetKind::BankAccount, kronor(864000));
        addHolding("flat", "Flat", domain::AssetKind::RealEstate, kronor(2200000));
        addHolding("index-funds", "Index funds", domain::AssetKind::Fund, kronor(5000000),
                   domain::Bucket::Preserve);
        // Growth is optional in this phase; this is what the household still
        // keeps invested beyond the years it plans to spend.
        addHolding("growth-fund", "Growth fund", domain::AssetKind::Fund, kronor(500000));
        addHolding("play", "Play money", domain::AssetKind::Equity, kronor(25000));

        // There is no mortgage left, which is why there is no loan here.
        break;
    }
    case ExampleProfile::FinanciallyIndependent: {
        // The same 24 000 kr a month of living costs, now paid from the
        // buckets: the phase moves money out of them rather than into them.
        flow = monthlyFlow(kronor(24000), kronor(14000), kronor(10000), kronor(0));

        // The 5 + 25 shape in cash and preserve: five years of expenses in the
        // buffer (1 440 000) and twenty-five years in index funds (7 200 000).
        // Growth and play are gone and the property is sold, because the model
        // says that once the game is won you stop playing.
        addHolding("buffer", "Buffer account", domain::AssetKind::BankAccount, kronor(1440000));
        addHolding("index-funds", "Index funds", domain::AssetKind::Fund, kronor(7200000),
                   domain::Bucket::Preserve);
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
