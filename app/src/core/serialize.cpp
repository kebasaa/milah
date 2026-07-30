#include "core/serialize.h"

#include "core/alignment.h"
#include "core/books.h"

#include <algorithm>

namespace milah {
namespace {

QString escapeXml(const QString &value)
{
    QString result = value;
    result.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    result.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    result.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    result.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    result.replace(QLatin1Char('\''), QLatin1String("&apos;"));
    return result;
}

struct Anchor
{
    int offset = 0;
    QString markup;
};

QString verseBody(
    const QString &verseId,
    const QString &text,
    const CombinedApparatus &apparatus)
{
    QList<Anchor> anchors;

    const auto notes = apparatus.notes.constFind(verseId);
    if (notes != apparatus.notes.constEnd()) {
        for (const SourceNote &note : notes.value()) {
            Anchor anchor;
            anchor.offset = note.charOffset;
            anchor.markup =
                QStringLiteral("<note type=\"explanation\" placement=\"foot\" n=\"%1\""
                               " osisRef=\"%2\" osisID=\"%2!note.%1\">%3</note>")
                    .arg(escapeXml(note.number), escapeXml(verseId), escapeXml(note.text));
            anchors.append(anchor);
        }
    }

    for (const SourceMilestone &milestone : apparatus.milestones) {
        if (!milestone.verseId.has_value() || *milestone.verseId != verseId) {
            continue;
        }
        Anchor anchor;
        anchor.offset = milestone.charOffset;
        anchor.markup = QStringLiteral("<milestone type=\"%1\" n=\"%2\"/>")
                            .arg(escapeXml(milestone.type), escapeXml(milestone.n));
        anchors.append(anchor);
    }

    // Stable, so notes stay ahead of milestones anchored at the same offset.
    std::stable_sort(
        anchors.begin(),
        anchors.end(),
        [](const Anchor &left, const Anchor &right) { return left.offset < right.offset; });

    QString out;
    int cursor = 0;
    for (const Anchor &anchor : anchors) {
        const int offset =
            std::max(cursor, std::min(anchor.offset, int(text.size())));
        out += escapeXml(text.mid(cursor, offset - cursor));
        out += anchor.markup;
        cursor = offset;
    }
    out += escapeXml(text.mid(cursor));
    return out;
}

QString titleMarkup(const SourceTitle &title)
{
    const QString canonical =
        title.canonical ? QStringLiteral(" canonical=\"true\"") : QString();
    return QStringLiteral("      <title type=\"%1\"%2>%3</title>\n")
        .arg(escapeXml(title.type), canonical, escapeXml(title.text));
}

} // namespace

QString serializeCombinedOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const WorkMetadata &metadata,
    const CombinedApparatus &apparatus)
{
    QList<CombinedDraft> ordered = drafts.values();
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const CombinedDraft &left, const CombinedDraft &right) {
            const int bookOrder =
                compareBooks(left.reference.book, right.reference.book);
            if (bookOrder != 0) {
                return bookOrder < 0;
            }
            if (left.reference.chapter != right.reference.chapter) {
                return left.reference.chapter < right.reference.chapter;
            }
            return compareNumericAware(left.reference.verse, right.reference.verse) < 0;
        });

    const QString workId = metadata.workId.isEmpty()
        ? QStringLiteral("Milah.Combined")
        : metadata.workId;
    const QString language =
        metadata.language.isEmpty() ? QStringLiteral("he") : metadata.language;
    const QString title = metadata.title.isEmpty()
        ? QStringLiteral("Milah Combined Edition")
        : metadata.title;

    QString body;
    QString currentBook;
    int currentChapter = -1;

    const auto closeChapter = [&] {
        if (currentChapter >= 0) {
            body += QStringLiteral("      <chapter eID=\"%1.%2\"/>\n")
                        .arg(escapeXml(currentBook))
                        .arg(currentChapter);
        }
    };

    for (const CombinedDraft &draft : ordered) {
        const VerseReference &reference = draft.reference;

        if (reference.book != currentBook) {
            closeChapter();
            if (!currentBook.isEmpty()) {
                body += QStringLiteral("    </div>\n");
            }
            currentBook = reference.book;
            currentChapter = -1;
            body += QStringLiteral("    <div type=\"book\" osisID=\"%1\">\n")
                        .arg(escapeXml(currentBook));
            for (const SourceTitle &sourceTitle : apparatus.titles) {
                if (sourceTitle.book.has_value() && *sourceTitle.book == currentBook
                    && !sourceTitle.chapter.has_value()) {
                    body += titleMarkup(sourceTitle);
                }
            }
        }

        if (reference.chapter != currentChapter) {
            closeChapter();
            currentChapter = reference.chapter;
            for (const SourceTitle &sourceTitle : apparatus.titles) {
                if (sourceTitle.book.has_value() && *sourceTitle.book == currentBook
                    && sourceTitle.chapter.has_value()
                    && *sourceTitle.chapter == currentChapter) {
                    body += titleMarkup(sourceTitle);
                }
            }
            body += QStringLiteral("      <chapter sID=\"%1.%2\" osisID=\"%1.%2\"/>\n")
                        .arg(escapeXml(currentBook))
                        .arg(currentChapter);
        }

        // Milestone form, so that titles and folio boundaries can sit between
        // or inside verses without nesting inside them.
        body += QStringLiteral("        <verse sID=\"%1\" osisID=\"%1\" n=\"%2\"/>%3"
                               "<verse eID=\"%1\"/>\n")
                    .arg(
                        escapeXml(reference.id),
                        escapeXml(reference.verse),
                        verseBody(reference.id, combinedText(draft), apparatus));
    }

    closeChapter();
    if (!currentBook.isEmpty()) {
        body += QStringLiteral("    </div>\n");
    }

    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<osis xmlns=\"http://www.bibletechnologies.net/2003/OSIS/namespace\">\n"
               "  <osisText osisIDWork=\"%1\" osisRefWork=\"bible\" xml:lang=\"%2\">\n"
               "    <header>\n"
               "      <work osisWork=\"%1\">\n"
               "        <title>%3</title>\n"
               "        <description>Combined edition generated by Milah.</description>\n"
               "        <type type=\"x-bible\">Edition</type>\n"
               "        <identifier type=\"OSIS\">%1</identifier>\n"
               "        <language>%2</language>\n"
               "      </work>\n"
               "      <work osisWork=\"bible\">\n"
               "        <title>Referenced versification</title>\n"
               "        <identifier type=\"OSIS\">bible</identifier>\n"
               "        <language>%2</language>\n"
               "        <refSystem>StandardV11N</refSystem>\n"
               "      </work>\n"
               "    </header>\n"
               "%4  </osisText>\n"
               "</osis>\n")
        .arg(escapeXml(workId), escapeXml(language), escapeXml(title), body);
}

} // namespace milah
