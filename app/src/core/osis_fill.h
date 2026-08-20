#pragma once

#include "core/types.h"

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace milah {

/// What one line of a folio comes out of a fill as: the words, and a box each.
///
/// Here rather than beside the window that used to be the only thing producing
/// them. Continuing a transcription produces them with no window at all, and
/// both hand the same shape to the same code that lays them onto the leaf.
struct FilledLine
{
    /// The recogniser's own index for the line, which is what a word carries.
    int index = 0;
    QStringList words;
    QList<QRect> boxes;
};

/// Reading a stretch of a published transcription out, ready to be laid onto a
/// folio.
///
/// Its own file because two callers need exactly the same answer and used to
/// each have their own: the fill window, where a transcriber picks a book and a
/// verse, and the continuation, which takes them off the folio before. Two
/// definitions of "the passage starting here" is one more than the number that
/// can be right, and the difference between them is invisible until a folio
/// comes out repeating four words the leaf before it already had.
struct Passage
{
    /// Every word, in reading order.
    QStringList words;
    /// The verse each word came from, one per word of `words`, as an OSIS id —
    /// so a folio can be cut into verses the way the source is.
    QStringList verses;

    bool isEmpty() const { return words.isEmpty(); }
};

/// Which of `source`'s books this name means, in the source's own spelling, or
/// empty where it holds no such book.
///
/// **Case-insensitively**, because the two halves of Milah spell a book
/// differently and always have: a folio carries Milah's own code, `JAS`, and an
/// OSIS carries `Jas`. Comparing them exactly is a match that silently never
/// happens — the continuation would simply decide there was nothing to carry on.
QString bookNamed(const SourceDocument &source, const QString &book);

/// Every word of `book` from `chapter`:`firstVerse` to the end of the book, with
/// the first `skip` words dropped.
///
/// To the end of the book rather than the end of the chapter: a folio runs over
/// a chapter break as readily as not, and stopping at one would leave the bottom
/// of the leaf empty for no reason the manuscript knows about.
///
/// **The verse numbers the scribe wrote are words of the passage.** In a
/// manuscript that numbers its verses in the running text, as Oo.1.32 does with
/// Arabic digits, the segmenter finds a box for each numeral, and a pour that
/// walks past them lays the verse's first word onto the number's box and puts
/// every word after it one place out for the rest of the leaf. So a verse opens
/// with its own number, taken from the OSIS `n=` attribute — except the first
/// verse of a chapter, which these manuscripts leave unnumbered, and a verse the
/// edition prints with no text at all.
///
/// `skip` is how many words of `firstVerse` the folio before this one already
/// holds, because a leaf ends mid-verse far more often than not. A skip past the
/// end of the passage gives nothing rather than reading off the end of it.
Passage gatherPassage(
    const SourceDocument &source,
    const QString &book,
    int chapter,
    int firstVerse,
    int skip);

} // namespace milah
