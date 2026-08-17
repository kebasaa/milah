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
            if (!passage.words.isEmpty()
                && (joinsToTheNext(passage.words.last()) || isPunctuationAlone(text))) {
                passage.words.last() += text;
                continue;
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
