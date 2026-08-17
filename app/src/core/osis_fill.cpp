#include "core/osis_fill.h"

#include <algorithm>

namespace milah {

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
            if (token.text.trimmed().isEmpty()) {
                continue;
            }
            passage.words.append(token.text);
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
