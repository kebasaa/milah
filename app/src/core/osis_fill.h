#pragma once

#include "core/transcription.h"
#include "core/types.h"

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

#include <optional>

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

/// `passage` as a manuscript without pointing writes it: without its vowel
/// points, its accents, or both, for pouring a pointed and cantillated edition
/// onto a leaf that has neither. A word left empty — a lone accent — is kept,
/// so the words stay in step with their verses.
Passage withoutMarks(const Passage &passage, bool points, bool accents);

/// A work that is not in OSIS, as a fill source: plain UTF-8 text divided into
/// sections by blank lines, each section a "verse" of chapter 1 of a book
/// named after `name` (letters and digits only, "Text" when nothing is left).
/// A mishnah, a liturgical poem or a letter has no verses, but a fill needs a
/// place to start from and a place to carry on from, and a numbered section is
/// both. A file with no blank lines is one section per line.
SourceDocument plainTextSource(const QString &text, const QString &name);

/// The work type plainTextSource() gives what it makes, so a fill knows not to
/// pour verse numerals for its sections.
inline constexpr const char kPlainTextWork[] = "x-plain-text";

/// The published text a fill pours from, read and parsed with the same reader
/// the comparison uses, so a file that opens there opens here. A `.txt`
/// file is read with plainTextSource(). Empty, with
/// `error` set to something fit to show a person, when the file cannot be
/// opened or is not OSIS.
std::optional<SourceDocument> readFillSource(const QString &path, QString *error);

/// The lines of a folio a fill may pour into, in order: the boxes each holds,
/// from fillableLines() so marginalia are left out, and how many words each
/// takes, from wordCounts() where the transcriber has said and the box count
/// otherwise. From `fromLine` down, for a re-flow below a held-out box.
struct FolioLines
{
    QList<int> indices;
    QList<QList<QRect>> boxes;
    QList<int> counts;

    bool isEmpty() const { return indices.isEmpty(); }
};
FolioLines folioLines(const TranscribedPage &page, int fromLine = 0);

/// What a fill comes to: the lines with their words and boxes, which verse
/// each word belongs to, and how many words of the passage were used.
struct Pour
{
    QList<FilledLine> filled;
    QStringList wordVerses;
    int taken = 0;
};

/// `words` (with the verse of each in `verses`) laid onto `lines` from the
/// `startLine`-th, `counts` words to a line — the lines' own counts, or ones a
/// person has nudged. The one pour every fill goes through: the window's,
/// continuing onto the next folio, and laying the rest again after a box is
/// held out, which used to be three copies of this loop.
Pour pourPassage(const FolioLines &lines,
                 const QList<int> &counts,
                 const QStringList &words,
                 const QStringList &verses,
                 int startLine);

} // namespace milah
