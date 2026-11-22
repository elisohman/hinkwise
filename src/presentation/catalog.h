// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "domain/asset.h"
#include "domain/bucket.h"
#include "domain/liability.h"

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

/// Every label, in the domain's order, ready for a picker whose index is the
/// enum value.
QStringList assetKindLabels();
QStringList liabilityKindLabels();
QStringList bucketLabels();

} // namespace hinkwise::presentation::catalog
