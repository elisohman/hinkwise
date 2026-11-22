// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jurisdiction.h"

namespace hinkwise::domain {

Jurisdiction swedishJurisdiction()
{
    // ISK schablonskatt is roughly 1 % of capital per year; the app treats it
    // as a drag rather than modelling the tax base, because the model only
    // ever uses it to shift the multiplier band.
    Jurisdiction jurisdiction;
    jurisdiction.id = "SE";
    jurisdiction.safeWithdrawalRate = Percentage::fromBasisPoints(400);
    jurisdiction.capitalTaxDrag = Percentage::fromBasisPoints(100);
    jurisdiction.fiMultiplierLow = 21;
    jurisdiction.fiMultiplierDefault = 25;
    jurisdiction.fiMultiplierHigh = 33;
    return jurisdiction;
}

Jurisdiction customJurisdiction(Percentage safeWithdrawalRate,
                                Percentage capitalTaxDrag,
                                int fiMultiplierLow,
                                int fiMultiplierDefault,
                                int fiMultiplierHigh)
{
    Jurisdiction jurisdiction;
    jurisdiction.id = "custom";
    jurisdiction.safeWithdrawalRate = safeWithdrawalRate;
    jurisdiction.capitalTaxDrag = capitalTaxDrag;
    jurisdiction.fiMultiplierLow = fiMultiplierLow;
    jurisdiction.fiMultiplierDefault = fiMultiplierDefault;
    jurisdiction.fiMultiplierHigh = fiMultiplierHigh;
    return jurisdiction;
}

} // namespace hinkwise::domain
