// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "insight.h"

namespace hinkwise::domain {

const char* labelOf(Severity severity)
{
    switch (severity) {
    case Severity::Critical:
        return "Critical";
    case Severity::Warning:
        return "Warning";
    case Severity::Advice:
        return "Advice";
    case Severity::Info:
        return "Info";
    }
    return "Info";
}

} // namespace hinkwise::domain
