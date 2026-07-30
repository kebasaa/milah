#pragma once

#include "core/alignment.h"
#include "core/types.h"

#include <QList>
#include <QStringList>

namespace milah {

/// Book/chapter pairs every manuscript covers, in canonical order.
QList<Location> commonLocations(const DocumentRefs &manuscripts);

/// Every verse id any manuscript has at this location, in reading order.
QStringList verseIdsAtLocation(
    const DocumentRefs &manuscripts,
    const Location &location);

} // namespace milah
