#include "core/serialize.h"

#include "core/alignment.h"
#include "core/books.h"
#include "core/transcription.h"

#include <QMultiHash>

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

/// One note, as OSIS writes it.
///
/// In one place because two editions write notes now — the comparison anchors
/// them by character offset in running text, the transcription by which word
/// they belong to — and a remark made on one side of Milah has to come out
/// looking exactly like a remark made on the other.
QString noteMarkup(const SourceNote &note, const QString &verseId)
{
    return QStringLiteral("<note type=\"explanation\" placement=\"foot\" n=\"%1\""
                          " osisRef=\"%2\" osisID=\"%2!note.%1\">%3</note>")
        .arg(escapeXml(note.number), escapeXml(verseId), escapeXml(note.text));
}

/// `verseId` is what the notes are keyed by; `anchorRef` is what they point at.
///
/// The same string for every verse, and not for a preamble: a preamble's notes
/// are held under Book.Chapter.0 like any other verse's, but that verse is not
/// in the file — it is written as a div introducing the chapter — so a note
/// pointing at it would point at nothing. See serializeOsis.
QString verseBody(
    const QString &verseId,
    const QString &anchorRef,
    const QString &text,
    const CombinedApparatus &apparatus)
{
    QList<Anchor> anchors;

    const auto notes = apparatus.notes.constFind(verseId);
    if (notes != apparatus.notes.constEnd()) {
        for (const SourceNote &note : notes.value()) {
            Anchor anchor;
            anchor.offset = note.charOffset;
            anchor.markup = noteMarkup(note, anchorRef);
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

/// The verse as separately marked words, each carrying its gloss where one is
/// aligned to it. Only for the interlinear edition; the plain one writes the
/// verse as running text.
QString interlinearBody(
    const QString &verseId,
    const QString &anchorRef,
    const CombinedDraft &draft,
    const QMap<int, QString> &glosses,
    const CombinedApparatus &apparatus)
{
    if (draft.manualText.has_value()) {
        // One string the columns no longer describe, so there is nothing to
        // hang a gloss on.
        return escapeXml(*draft.manualText);
    }

    // Anchored by which word they belong to, not by how many characters in it
    // is: this body writes each word separately and skips the empty ones, so a
    // character offset into the running text would point at nothing here.
    // SourceNote::tokenIndex says exactly this and has had no other use.
    QMultiHash<int, SourceNote> anchored;
    const auto notes = apparatus.notes.constFind(verseId);
    if (notes != apparatus.notes.constEnd()) {
        for (const SourceNote &note : notes.value()) {
            anchored.insert(note.tokenIndex, note);
        }
    }

    QStringList words;
    for (int index = 0; index < draft.columns.size(); ++index) {
        const QString word = draft.columns.at(index).text.value_or(QString());
        if (word.isEmpty()) {
            continue;
        }

        QString marked;
        // In the order they were given, which editorApparatus has already put
        // in the order they appear.
        const QList<SourceNote> here = anchored.values(index);
        for (auto note = here.crbegin(); note != here.crend(); ++note) {
            marked += noteMarkup(*note, anchorRef);
        }

        const QString gloss = glosses.value(index);
        marked += gloss.isEmpty()
            ? QStringLiteral("<w>%1</w>").arg(escapeXml(word))
            : QStringLiteral("<w gloss=\"%1\">%2</w>")
                  .arg(escapeXml(gloss), escapeXml(word));
        words.append(marked);
    }
    return words.join(QLatin1Char(' '));
}

/// Both editions come through here: they differ only in how a verse's body is
/// written, so the document around it cannot drift between them.
QString serializeOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const WorkMetadata &metadata,
    const CombinedApparatus &apparatus,
    const InterlinearGlosses *glosses)
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

    // What the editor said about the work, which used to be assembled and then
    // dropped on the floor here. A transcription's shelfmark is how a
    // manuscript is identified at all, so a file that loses it is one nobody
    // can place — and the published texts carry theirs the same way.
    QString identifiers;
    for (auto item = metadata.identifiers.constBegin();
         item != metadata.identifiers.constEnd();
         ++item) {
        if (item.value().isEmpty()) {
            continue;
        }
        identifiers += QStringLiteral("        <identifier type=\"%1\">%2</identifier>\n")
                           .arg(escapeXml(item.key()), escapeXml(item.value()));
    }
    const QString scope = metadata.scope.isEmpty()
        ? QString()
        : QStringLiteral("        <scope>%1</scope>\n").arg(escapeXml(metadata.scope));

    // What the header says about the manuscript that no other element holds:
    // which folios, what it is written on, what the Hebrew renders, which older
    // book it copies. Written between the description above and the type below
    // rather than after them, because <work>'s children are a schema sequence
    // and a description that follows <type> is out of it.
    QString descriptions;
    for (auto item = metadata.descriptions.constBegin();
         item != metadata.descriptions.constEnd();
         ++item) {
        if (item.value().text.isEmpty()) {
            continue;
        }
        // The verdict, where there is one. Written as an attribute rather than
        // folded into the prose so that a reader of the file can tell "Greek"
        // from "Greek, probably" without parsing English.
        const QString subType = item.value().subType.isEmpty()
            ? QString()
            : QStringLiteral(" subType=\"%1\"").arg(escapeXml(item.value().subType));
        descriptions +=
            QStringLiteral("        <description type=\"%1\"%2>%3</description>\n")
                .arg(escapeXml(item.key()), subType, escapeXml(item.value().text));
    }

    // The published library labels its manuscripts one way and its editions
    // another, and a transcription filed beside them should read as what it is.
    const bool manuscript = metadata.workType == QLatin1String("x-manuscript");
    const QString workType =
        QStringLiteral("        <description>%1</description>\n%2"
                       "        <type type=\"%3\">%4</type>\n")
            .arg(
                manuscript ? QStringLiteral("Transcribed with Milah.")
                           : QStringLiteral("Combined edition generated by Milah."),
                descriptions,
                manuscript ? QStringLiteral("x-manuscript") : QStringLiteral("x-bible"),
                manuscript ? QStringLiteral("Manuscript") : QStringLiteral("Edition"));

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

        // What the chapter this belongs to is called, which a preamble points
        // at in place of a verse of its own.
        const QString chapterRef =
            QStringLiteral("%1.%2").arg(escapeXml(currentBook)).arg(currentChapter);
        const bool preamble = isPreamble(reference.verse);
        const QString anchorRef = preamble ? chapterRef : escapeXml(reference.id);

        const QString written = glosses
            ? interlinearBody(
                  reference.id, anchorRef, draft, glosses->value(reference.id), apparatus)
            : verseBody(reference.id, anchorRef, combinedText(draft), apparatus);

        if (preamble) {
            // Matter standing before verse 1 — an incipit, a superscription,
            // the scribe's heading. OSIS has an element for it, and it is not a
            // verse: numbering it 0 would address a verse no versification has.
            // The drafts are sorted with 0 first within its chapter, so this is
            // already in the right place.
            body += QStringLiteral("        <div type=\"introduction\" osisRef=\"%1\">%2"
                                   "</div>\n")
                        .arg(chapterRef, written);
            continue;
        }

        // Milestone form, so that titles and folio boundaries can sit between
        // or inside verses without nesting inside them.
        body += QStringLiteral("        <verse sID=\"%1\" osisID=\"%1\" n=\"%2\"/>%3"
                               "<verse eID=\"%1\"/>\n")
                    .arg(escapeXml(reference.id), escapeXml(reference.verse), written);
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
               "%4"
               "        <identifier type=\"OSIS\">%1</identifier>\n"
               "%5"
               "        <language>%2</language>\n"
               "%6"
               "      </work>\n"
               "      <work osisWork=\"bible\">\n"
               "        <title>Referenced versification</title>\n"
               "        <identifier type=\"OSIS\">bible</identifier>\n"
               "        <language>%2</language>\n"
               "        <refSystem>StandardV11N</refSystem>\n"
               "      </work>\n"
               "    </header>\n"
               "%7  </osisText>\n"
               "</osis>\n")
        // The text itself goes in last, together with the header's free fields:
        // a multi-argument arg() does not rescan what it substituted, so a note
        // that happens to read "%1" stays a note rather than becoming the title.
        .arg(escapeXml(workId), escapeXml(language), escapeXml(title), workType)
        .arg(identifiers, scope, body);
}

} // namespace

QString serializeCombinedOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const WorkMetadata &metadata,
    const CombinedApparatus &apparatus)
{
    return serializeOsis(drafts, metadata, apparatus, nullptr);
}

QString serializeInterlinearOsis(
    const QMap<QString, CombinedDraft> &drafts,
    const InterlinearGlosses &glosses,
    const WorkMetadata &metadata,
    const CombinedApparatus &apparatus)
{
    return serializeOsis(drafts, metadata, apparatus, &glosses);
}

} // namespace milah
