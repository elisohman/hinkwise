// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog.h"

#include <QCoreApplication>
#include <cstddef>

namespace hinkwise::presentation::catalog {

namespace {

QString translated(const char* source)
{
    return QCoreApplication::translate("Catalog", source);
}

} // namespace

QString assetKindLabel(domain::AssetKind kind)
{
    return QString::fromUtf8(domain::factsOf(kind).label);
}

QString liabilityKindLabel(domain::LiabilityKind kind)
{
    switch (kind) {
    case domain::LiabilityKind::Mortgage:
        return translated("Mortgage");
    case domain::LiabilityKind::StudentLoan:
        return translated("Student loan");
    case domain::LiabilityKind::ConsumerLoan:
        return translated("Consumer loan");
    case domain::LiabilityKind::CreditCard:
        return translated("Credit card");
    case domain::LiabilityKind::CarLoan:
        return translated("Car loan");
    case domain::LiabilityKind::Other:
        return translated("Other");
    }
    return translated("Other");
}

QString bucketLabel(domain::Bucket bucket)
{
    return QString::fromUtf8(domain::labelOf(bucket));
}

QStringList assetKindLabels()
{
    QStringList labels;
    for (int index = 0; index <= static_cast<int>(domain::AssetKind::Other); ++index)
        labels.append(assetKindLabel(static_cast<domain::AssetKind>(index)));
    return labels;
}

QStringList liabilityKindLabels()
{
    QStringList labels;
    for (int index = 0; index <= static_cast<int>(domain::LiabilityKind::Other); ++index)
        labels.append(liabilityKindLabel(static_cast<domain::LiabilityKind>(index)));
    return labels;
}

QStringList bucketLabels()
{
    QStringList labels;
    for (const domain::Bucket bucket : domain::allBuckets())
        labels.append(bucketLabel(bucket));
    return labels;
}

} // namespace hinkwise::presentation::catalog
