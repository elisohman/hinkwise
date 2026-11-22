// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace hinkwise::domain {

/// How loudly an insight asks to be acted on. Ordered from most to least
/// urgent so a list can simply be sorted.
enum class Severity {
    Critical = 0,
    Warning = 1,
    Advice = 2,
    Info = 3,
};

const char* labelOf(Severity severity);

/// A single observation about the household, phrased as coaching rather than
/// judgement: what is happening, why it matters to the model, what to do.
struct Insight {
    std::string id;
    Severity severity = Severity::Info;
    std::string title;
    std::string why;
    std::string suggestion;
};

} // namespace hinkwise::domain
