#include "core/osis_fill.h"

#include <algorithm>

namespace milah {
namespace {

/// Whether this token carries the joiner that binds it to the one after it.
///
/// The tokeniser leaves a maqqef or hyphen on the *first* piece of a compound —
/// see core/tokenize.cpp, which explains why it belongs there — so a token
/// ending in one is half of a word rather than a word.
bool joinsToTheNext(const QString &token)
{
    if (token.isEmpty()) {
        return false;
    }
    const QChar last = token.back();
    return last == QChar(0x05BE) || last == QLatin1Char('-');
}

/// Whether this token is punctuation on its own, with no word in it.
///
/// The tokeniser ends with a fallback that matches any single character it did
/// not otherwise recognise, so a sof pasuq or gershayim written against a word —
/// `רעים׃` — comes out as the word and then the mark. That is right for
/// collation, which wants to align the words; it is wrong here, because the
/// scribe wrote the mark against the word, in the same box, and a token of its
/// own would take a box from the line and push everything after it along.
bool isPunctuationAlone(const QString &token)
{
    for (const QChar character : token) {
        if (character.isLetterOrNumber()) {
            return false;
        }
    }
    return !token.isEmpty();
}

/// The number the scribe wrote on the leaf to open this verse, or empty where
/// there is none to write.
///
/// **A word of the manuscript, not a label.** In Oo.1.32 the verse numbers are
/// written into the running text as Arabic digits — `2:`, `3:` … `20:` — so the
/// segmenter finds a box for each, and a fill that walks past them lays the
/// verse's first word onto the number's box and puts everything after it one
/// place out for the rest of the leaf. That is the same fault the maqqef and the
/// sof pasuq had, in a third disguise.
///
/// From the OSIS `n=` attribute, which core/osis.cpp keeps as `label` and fills
/// in from the verse number where the file omits it. **Not** from `altNumber`:
/// that carries whatever `subType="x-alt-…"` says, which in this edition is
/// another edition's reference — `2.15-16` — and pouring that would put a
/// citation on the folio where a digit belongs.
///
/// **Verse 1 gets nothing.** Twice on these folios the numbering starts at 2:
/// James opens `יעקב עבד ה` with no numeral, and after the `פרק` heading on 159r
/// the next chapter opens the same way. The number introduces a verse against
/// the one before it, and the first verse of a chapter has nothing to be told
/// apart from.
///
/// The colon written beside the digit is not poured. The OSIS does not record
/// it — a verse's text begins at its first word — and inventing a character for
/// ground truth is worse than leaving a box for the transcriber to type into.
QString verseMarker(const SourceVerse &verse)
{
    if (verse.reference.verse.toInt() <= 1) {
        return QString();
    }
    return verse.label.isEmpty() ? verse.reference.verse : verse.label;
}

} // namespace

QString bookNamed(const SourceDocument &source, const QString &book)
{
    if (book.isEmpty()) {
        return QString();
    }
    for (const SourceVerse &verse : source.verses) {
        if (verse.reference.book.compare(book, Qt::CaseInsensitive) == 0) {
            // The source's spelling, not the one asked with: everything
            // downstream compares against the source's own books.
            return verse.reference.book;
        }
    }
    return QString();
}

Passage gatherPassage(
    const SourceDocument &source,
    const QString &book,
    int chapter,
    int firstVerse,
    int skip)
{
    Passage passage;
    const QString wanted = bookNamed(source, book);
    if (wanted.isEmpty()) {
        return passage;
    }

    bool started = false;
    for (const SourceVerse &verse : source.verses) {
        if (verse.reference.book != wanted) {
            continue;
        }
        if (!started) {
            const bool here = verse.reference.chapter > chapter
                || (verse.reference.chapter == chapter
                    && verse.reference.verse.toInt() >= firstVerse);
            if (!here) {
                continue;
            }
            started = true;
        }
        // Ahead of the verse's own words, because that is where the scribe put
        // it: the numeral stands between the last word of the verse before and
        // the first of this one, and it belongs to the verse it opens.
        //
        // Laid down on the first word rather than before the loop, so a verse
        // the edition prints empty gets no numeral. This one prints Jas 1:21
        // with no text at all, because the manuscript has none — and a number
        // written onto the leaf for a verse that is not on it would take a box
        // from the verse that is.
        bool opened = false;
        const QString marker = verseMarker(verse);
        for (const SourceToken &token : verse.tokens) {
            const QString text = token.text.trimmed();
            if (text.isEmpty()) {
                continue;
            }
            // A compound joined at a maqqef is one word on the leaf, so it is
            // one word here. core/tokenize.cpp splits it on purpose — for
            // collation, where אֲנִי־יוֹחָנָן has to line up against a witness
            // that writes two words — but a fill is not collation. The scribe
            // wrote it once, in one box, and pouring it as two lays an extra
            // word on the line and puts everything below it one place late for
            // the rest of the leaf.
            //
            // Before the numeral, and on purpose: a mark that opens a verse in
            // the source was written against the *previous* verse's last word,
            // which is where it goes here too.
            if (!passage.words.isEmpty()
                && (joinsToTheNext(passage.words.last()) || isPunctuationAlone(text))) {
                passage.words.last() += text;
                continue;
            }
            if (!opened) {
                opened = true;
                if (!marker.isEmpty()) {
                    passage.words.append(marker);
                    passage.verses.append(verse.reference.id);
                }
            }
            passage.words.append(text);
            passage.verses.append(verse.reference.id);
        }
    }

    // Dropped here rather than carried alongside as an offset. An offset has to
    // be remembered by everything downstream and was not: the words the previous
    // leaf already holds are simply not part of this folio's passage.
    const int drop = std::clamp(skip, 0, int(passage.words.size()));
    passage.words = passage.words.mid(drop);
    passage.verses = passage.verses.mid(drop);
    return passage;
}

} // namespace milah
