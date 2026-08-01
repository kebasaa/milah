#pragma once

#include <QString>

namespace milah {

class UserDictionary;

/// The one-line verdict on a word: which corpus knows it, and what by.
///
/// H1234 where the lexicon knows exactly one entry. H1234? where it knows
/// several — the likeliest is shown and the alternatives kept in the tooltip
/// rather than one of them being passed off as the answer. M where the Hebrew
/// Bible has never heard the word but the Mishnah or the Tosefta has, because
/// Strong's covers the Bible only and a post-biblical word has no number and
/// never will. An em dash where nothing knows it — which says the word is
/// unattested, not that it is wrong.
///
/// The editor's own definitions ride beside all of that as ·D, so an annotated
/// word can be seen without hovering. The exception is the dash: a dash says
/// nothing knows this word, which stops being true the moment the editor
/// defines it, so D stands alone there rather than riding on a dash it
/// contradicts.
struct WordMarker
{
    QString text;
    QString tooltip;
};

/// Reads the marker for a word exactly as it was typed or edited. The lookups
/// normalise for themselves — pointing, cantillation and attached prefixes are
/// all handled inside the lexicon — so nothing has to be stripped first.
///
/// An empty word gives an empty marker rather than a dash: a column with no
/// reading in it is not a word nobody knows.
WordMarker markerFor(const QString &word, const UserDictionary &dictionary);

/// What the Strong's row says about itself, as a tooltip on its own name.
/// Shared so the comparison and the transcription explain the column the same
/// way.
QString markerRowTooltip();

} // namespace milah
