// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "percentage.h"

#include <string>

namespace hinkwise::domain {

/// The tax and withdrawal assumptions that turn annual expenses into a target
/// capital. Sweden is the default because the model is Swedish, but every
/// value is editable: the arithmetic does not depend on the jurisdiction,
/// only the constants do.
///
/// The multiplier band comes straight from the model: x21 corresponds to a
/// 4.7 % withdrawal rate, x25 to the classic 4 %, and x33 to 3 % net of the
/// 1 % capital tax drag. The spread is the point - it is wider than any
/// disagreement in the research.
struct Jurisdiction {
    std::string id;
    /// Withdrawal rate before tax, e.g. 4.00 %.
    Percentage safeWithdrawalRate{Percentage::fromBasisPoints(400)};
    /// Annual drag on invested capital, e.g. ISK schablonskatt ~1 %.
    Percentage capitalTaxDrag{Percentage::fromBasisPoints(100)};
    int fiMultiplierLow = 21;
    int fiMultiplierDefault = 25;
    int fiMultiplierHigh = 33;
};

Jurisdiction swedishJurisdiction();

Jurisdiction customJurisdiction(Percentage safeWithdrawalRate,
                                Percentage capitalTaxDrag,
                                int fiMultiplierLow,
                                int fiMultiplierDefault,
                                int fiMultiplierHigh);

} // namespace hinkwise::domain
