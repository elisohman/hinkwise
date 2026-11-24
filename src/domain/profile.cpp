// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profile.h"

namespace hinkwise::domain {

const char* labelOf(TrackingMode mode)
{
    switch (mode) {
    case TrackingMode::Accounting:
        return "Monthly accounting";
    case TrackingMode::Planning:
        return "Budget planning";
    }
    return "Unknown";
}

} // namespace hinkwise::domain
