#include "core/transcription_docx.h"

#include "core/books.h"

namespace milah {
namespace {

/// True when anything has actually been read off this verse. A folio always
/// carries an empty verse holding an empty word — there has to be somewhere to
/// type — and printing that would be a document of blank lines.
bool hasText(const TranscribedVerse &verse)
{
    for (const TranscribedWord &word : verse.words) {
        if (!word.hebrew.isEmpty() || !word.english.isEmpty()) {
            return true;
        }
    }
    return false;
}

/// What to call the book on this folio: what the transcriber wrote, or the
/// canonical name of the id they settled on. Empty when neither is known, which
/// is a folio nobody has identified yet rather than an error.
QString bookTitle(const TranscribedPage &page)
{
    if (!page.bookLabel.isEmpty()) {
        return page.bookLabel;
    }
    if (!page.book.isEmpty()) {
        return bookName(page.book);
    }
    return QString();
}

QString chapterHeading(const TranscribedPage &page, int chapter)
{
    const QString book = bookTitle(page);
    if (book.isEmpty()) {
        return QStringLiteral("Chapter %1").arg(chapter);
    }
    return QStringLiteral("%1 %2").arg(book).arg(chapter);
}

QString titleFor(const TranscriptionMetadata &metadata)
{
    return metadata.manuscriptName.isEmpty() ? QStringLiteral("Transcription")
                                             : metadata.manuscriptName;
}

/// What the manuscript is and who read it, one line each, skipping whatever was
/// left blank — the panel is a description, not a form to be completed.
QStringList subtitleFor(const TranscriptionMetadata &metadata)
{
    QStringList lines;
    const QString shelfmark =
        metadata.shelfmark.isEmpty() ? metadata.libraryMark : metadata.shelfmark;
    if (!shelfmark.isEmpty()) {
        lines.append(shelfmark);
    }
    QStringList origin;
    if (!metadata.origin.isEmpty()) {
        origin.append(metadata.origin);
    }
    if (!metadata.date.isEmpty()) {
        origin.append(metadata.date);
    }
    if (!origin.isEmpty()) {
        lines.append(origin.join(QStringLiteral(", ")));
    }
    if (!metadata.transcriber.isEmpty()) {
        lines.append(QStringLiteral("Transcribed by %1").arg(metadata.transcriber));
    }
    return lines;
}

/// The verse's words as runs, with a footnote reference standing immediately
/// after each word carrying a remark.
///
/// Words are gathered into as few runs as the notes allow rather than one run
/// apiece: a run is several lines of XML, and a chapter is a few thousand
/// words. The buffer is flushed only where a marker has to be planted, which is
/// what keeps each marker beside its own word instead of at the start of the
/// verse.
void appendVerseRuns(
    const TranscribedVerse &verse, DocxDocument &document, QList<DocxRun> &runs)
{
    QString pending;
    const auto flush = [&] {
        if (pending.isEmpty()) {
            return;
        }
        DocxRun run;
        run.text = pending;
        run.hebrew = true;
        runs.append(run);
        pending.clear();
    };

    for (const TranscribedWord &word : verse.words) {
        if (word.hebrew.isEmpty()) {
            continue;
        }
        if (!pending.isEmpty()) {
            pending += QLatin1Char(' ');
        }
        pending += word.hebrew;

        if (!word.note.isEmpty()) {
            flush();
            DocxRun marker;
            marker.footnoteId = document.addFootnote(word.note);
            runs.append(marker);
        }
    }
    flush();
}

/// True when the transcriber has glossed anything in this verse at all.
///
/// Asked before a gloss line is written, because glossLine() holds an unglossed
/// word's place with a dash — so a verse nobody has glossed would otherwise
/// come out as a line of dashes under every verse rather than no line at all.
bool hasAnyGloss(const TranscribedVerse &verse)
{
    for (const TranscribedWord &word : verse.words) {
        if (!word.hebrew.isEmpty() && !word.english.isEmpty()) {
            return true;
        }
    }
    return false;
}

/// The transcriber's glosses in the order their words stand, which is what makes
/// the line an interlinear rather than a translation.
QString glossLine(const TranscribedVerse &verse)
{
    QStringList glosses;
    for (const TranscribedWord &word : verse.words) {
        if (word.hebrew.isEmpty()) {
            continue;
        }
        // A word left unglossed keeps its place, so the count of the two lines
        // still matches and the reader can see which one was not answered.
        glosses.append(word.english.isEmpty() ? QStringLiteral("—") : word.english);
    }
    return glosses.join(QStringLiteral("  "));
}

DocxParagraph headingParagraph(const QString &text, const QString &style)
{
    DocxParagraph paragraph;
    paragraph.style = style;
    paragraph.runs.append(DocxRun{text});
    return paragraph;
}

DocxRun verseNumberRun(const QString &number)
{
    DocxRun run;
    run.text = number + QStringLiteral(" ");
    run.superscript = true;
    run.hebrew = true;
    return run;
}

} // namespace

DocxDocument readingWordDocument(const TranscriptionDocument &transcription)
{
    DocxDocument document;
    document.title = titleFor(transcription.metadata);
    document.subtitle = subtitleFor(transcription.metadata);

    // One paragraph per chapter, the verses running on inside it. That is what
    // makes this a text to read rather than a list of verses.
    DocxParagraph running;
    running.style = QStringLiteral("VerseHebrew");
    running.rightToLeft = true;

    QString openBook;
    int openChapter = -1;

    const auto closeChapter = [&] {
        if (!running.runs.isEmpty()) {
            document.blocks.append(running);
            running.runs.clear();
        }
    };

    for (const TranscribedPage &page : transcription.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            const TranscribedVerse &verse = page.verses.at(index);
            if (!hasText(verse)) {
                continue;
            }

            const int chapter = chapterOfVerse(page, index);
            if (page.book != openBook || chapter != openChapter) {
                closeChapter();
                document.blocks.append(
                    headingParagraph(chapterHeading(page, chapter), QStringLiteral("Heading1")));
                openBook = page.book;
                openChapter = chapter;
            }

            if (!running.runs.isEmpty()) {
                DocxRun gap;
                gap.text = QStringLiteral(" ");
                gap.hebrew = true;
                running.runs.append(gap);
            }
            // A preamble opens the chapter with no marker of its own. It stands
            // before verse 1 and is not verse 0, and a raised ⁰ in front of the
            // ¹ reads as a mistake — which is how a printed Bible sets a
            // superscription too.
            if (!verse.number.isEmpty() && !isPreamble(verse.number)) {
                running.runs.append(verseNumberRun(verse.number));
            }
            appendVerseRuns(verse, document, running.runs);
        }
    }
    closeChapter();

    return document;
}

DocxDocument interlinearWordDocument(const TranscriptionDocument &transcription)
{
    DocxDocument document;
    document.title = titleFor(transcription.metadata);
    document.subtitle = subtitleFor(transcription.metadata);

    QString openBook;
    int openChapter = -1;

    for (const TranscribedPage &page : transcription.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            const TranscribedVerse &verse = page.verses.at(index);
            if (!hasText(verse)) {
                continue;
            }

            const int chapter = chapterOfVerse(page, index);
            if (page.book != openBook || chapter != openChapter) {
                document.blocks.append(
                    headingParagraph(chapterHeading(page, chapter), QStringLiteral("Heading1")));
                openBook = page.book;
                openChapter = chapter;
            }

            // verseHeading says as much of the reference as is known, down to
            // "Unnumbered" — so a verse nobody has numbered yet is still
            // printed, under a name that admits what it is.
            document.blocks.append(
                headingParagraph(verseHeading(page, index), QStringLiteral("Heading2")));

            DocxParagraph hebrew;
            hebrew.style = QStringLiteral("VerseHebrew");
            hebrew.rightToLeft = true;
            appendVerseRuns(verse, document, hebrew.runs);
            document.blocks.append(hebrew);

            if (hasAnyGloss(verse)) {
                DocxParagraph gloss;
                gloss.style = QStringLiteral("Gloss");
                gloss.runs.append(DocxRun{glossLine(verse)});
                document.blocks.append(gloss);
            }
        }
    }

    return document;
}

int unnamedVerseCount(const TranscriptionDocument &transcription)
{
    int unnamed = 0;
    for (const TranscribedPage &page : transcription.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            if (hasText(page.verses.at(index))
                && transcribedVerseId(page, index).isEmpty()) {
                ++unnamed;
            }
        }
    }
    return unnamed;
}

} // namespace milah
