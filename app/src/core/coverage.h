#pragma once

#include "core/alignment.h"
#include "core/types.h"

#include <QList>
#include <QStringList>

namespace milah {

/// A place some manuscript covers, and how well.
struct LocationCoverage
{
    Location location;
    /// How many of the loaded manuscripts have any verse here.
    int sourceCount = 0;
    /// True when every one of them does. What the editor is shown when it is
    /// false is a chapter some witness is silent for, which is worth marking
    /// but not worth hiding.
    bool complete = false;
};

/// Every book and chapter **any** manuscript covers, in canonical order.
///
/// A union rather than an intersection: witnesses of different extent are the
/// normal case, and the places where their coverage differs are the ones an
/// editor most wants to reach. Chapters no manuscript has are not invented, so
/// a gap in a fragmentary witness stays a gap rather than an empty screen.
QList<LocationCoverage> coveredLocations(const DocumentRefs &manuscripts);

/// Every verse id any manuscript has at this location, in reading order.
QStringList verseIdsAtLocation(
    const DocumentRefs &manuscripts,
    const Location &location);

} // namespace milah
