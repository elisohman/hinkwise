// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "application/ports.h"
#include "application/result.h"
#include "domain/asset.h"
#include "domain/liability.h"
#include "domain/money.h"
#include "domain/planned_expense.h"
#include "domain/profile.h"
#include "domain/year_month.h"

#include <string>
#include <string_view>
#include <vector>

namespace hinkwise::infrastructure::json {

/// One amount at one month: a valuation of an asset or a balance of a debt.
struct MoneyPoint {
    domain::YearMonth month;
    domain::Money amount;
};

/// Every valuation of one asset, oldest first.
struct AssetValuations {
    std::string assetId;
    std::vector<MoneyPoint> points;
};

/// Every recorded balance of one debt, oldest first.
struct LiabilityBalances {
    std::string liabilityId;
    std::vector<MoneyPoint> points;
};

/// The whole household in one value: the shape of a backup file.
///
/// This is a transfer object, not the model. It exists so a backup can be read,
/// written and compared as a single thing, and so the file format has one
/// definition instead of being implied by whichever repository wrote last.
struct BackupData {
    domain::Profile profile;
    std::vector<domain::Asset> assets;
    std::vector<domain::Liability> liabilities;
    std::vector<domain::PlannedExpense> plannedExpenses;
    std::vector<AssetValuations> assetValuations;
    std::vector<LiabilityBalances> liabilityBalances;
    std::vector<application::MonthFlowRecord> monthFlows;
    std::vector<application::PhaseChange> phaseHistory;
};

/// The backup format this build writes and reads. A file from a newer build is
/// rejected rather than half understood: silently dropping a field the user
/// wrote is worse than refusing the file.
inline constexpr int kBackupVersion = 1;

/// The backup as indented JSON text.
///
/// Money is written as integer minor units with its currency, and months as
/// "YYYY-MM", so reading the file back yields exactly the values that went in.
std::string toJson(const BackupData& data);

/// Parses a backup written by toJson.
///
/// Fails with a message that names the field or the version when the text is
/// not JSON, comes from a version this build does not know, or is missing
/// something a household needs. Never throws.
application::Result<BackupData> backupFromJson(std::string_view text);

} // namespace hinkwise::infrastructure::json
