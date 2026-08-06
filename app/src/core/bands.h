#pragma once

#include <QList>

namespace milah {

/// A half-open run of columns drawn as one band.
struct Band
{
    int start = 0;
    int end = 0;
};

/// Cuts `widths` into bands that each fit `room`, with `spacing` between
/// adjacent columns.
///
/// Greedy first fit, taken in order and with no lookahead, because the columns
/// are words in sequence and may not be reordered to pack better. A band always
/// takes at least one column even where that column is wider than the room it
/// has: a word too long for the page still has to be drawn somewhere.
///
/// The unit is the caller's — pixels on screen, twentieths of a point in a Word
/// document. What must not differ between the two is where the breaks fall,
/// which is why this sits in the core rather than beside the widget that first
/// needed it.
QList<Band> packBands(const QList<int> &widths, int room, int spacing);

} // namespace milah
