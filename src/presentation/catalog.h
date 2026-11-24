// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/liability.h"
#include "domain/profile.h"

#include <QString>
#include <QStringList>

namespace hinkwise::presentation::catalog {

/// The names the interface uses for the domain's enumerations.
///
/// Asset kinds and buckets already have one spelling in the domain, because the
/// model reasons about them; liability kinds do not, because nothing in the
/// model reads a debt's kind, only its rate. These functions are the one
/// spelling for the screens, so the setup wizard and the buckets page cannot
/// offer different lists of the same thing.

QString assetKindLabel(domain::AssetKind kind);
QString liabilityKindLabel(domain::LiabilityKind kind);
QString bucketLabel(domain::Bucket bucket);

/// The untranslated handle the theme colours a bucket by, so every page that
/// draws one reads the same key out of the same function.
QString bucketColourKey(domain::Bucket bucket);

/// The two ways of keeping a record, as settings and onboarding offer them:
/// the name of the choice, and the two sentences that tell them apart. One
/// source for both screens, so the same choice is never described differently
/// in a wizard and in settings.
QString trackingModeLabel(domain::TrackingMode mode);
QString trackingModeDescription(domain::TrackingMode mode);

/// Every label, in the domain's order, ready for a picker whose index is the
/// enum value.
QStringList assetKindLabels();
QStringList liabilityKindLabels();
QStringList bucketLabels();

} // namespace hinkwise::presentation::catalog
