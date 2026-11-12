#include "core/osis.h"

#include "core/tokenize.h"
#include "core/transcription.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

namespace milah {
namespace {

constexpr const char *kOsisNamespace = "http://www.bibletechnologies.net/2003/OSIS/namespace";

const QSet<QString> &supportedInline()
{
    static const QSet<QString> tags = {
        QStringLiteral("verse"),        QStringLiteral("note"),
        QStringLiteral("w"),            QStringLiteral("seg"),
        QStringLiteral("hi"),           QStringLiteral("divineName"),
        QStringLiteral("name"),         QStringLiteral("foreign"),
        QStringLiteral("transChange"),  QStringLiteral("q"),
        QStringLiteral("reference"),    QStringLiteral("lb"),
        // Text the editor could not read with certainty is still the
        // witness's; its supplied and lost text are handled apart.
        QStringLiteral("unclear"),      QStringLiteral("supplied"),
    };
    return tags;
}

/// `subType="x-alt-14"` carries the source's own reference for a verse, where
/// that differs from the canonical one — a manuscript's Hebrew letter-numeral,
/// or another edition's chapter and verse.
const QRegularExpression &altNumberPattern()
{
    static const QRegularExpression expression(QStringLiteral("^x-alt-(.+)$"));
    return expression;
}

const QRegularExpression &forbiddenDeclarations()
{
    static const QRegularExpression expression(
        QStringLiteral("<!\\s*(?:DOCTYPE|ENTITY)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// Looks an attribute up by exact qualified name first, then by local name, so
/// that `xml:lang` answers to `lang` whatever prefix the document declares.
QString attributeValue(const QXmlStreamAttributes &attributes, QLatin1String name)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.qualifiedName() == name) {
            return attribute.value().toString();
        }
    }
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name() == name) {
            return attribute.value().toString();
        }
    }
    return QString();
}

bool isAllDigits(const QString &value)
{
    if (value.isEmpty()) {
        return false;
    }
    for (const QChar character : value) {
        if (character < u'0' || character > u'9') {
            return false;
        }
    }
    return true;
}

/// The ids in an osisID attribute, each without the parts Milah does not
/// address by: a work prefix ("Bible.KJV:Gen.1.1") and a grain ("Gen.1.1!a").
/// OSIS allows a space-separated list, for an element that carries several
/// verses at once.
QStringList osisIdsOf(const QString &attribute)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QStringList ids;
    for (QString id : attribute.split(whitespace, Qt::SkipEmptyParts)) {
        const qsizetype prefix = id.lastIndexOf(QLatin1Char(':'));
        if (prefix >= 0) {
            id = id.mid(prefix + 1);
        }
        const qsizetype grain = id.indexOf(QLatin1Char('!'));
        if (grain >= 0) {
            id.truncate(grain);
        }
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}

VerseReference parseReference(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("^([^.]+)\\.(\\d+)\\.(.+)$"));

    const QString first = osisIdsOf(id).value(0);
    const QRegularExpressionMatch match = pattern.match(first);
    if (!match.hasMatch()) {
        throw OsisError(QStringLiteral("Invalid verse osisID: %1").arg(id));
    }

    VerseReference reference;
    reference.id = first;
    reference.book = match.captured(1);
    reference.chapter = match.captured(2).toInt();
    reference.verse = match.captured(3);
    return reference;
}

struct PendingVerse
{
    VerseReference reference;
    QString label;
    QString text;
    QList<SourceNote> notes;
    bool milestone = false;
    /// The sID a milestone verse opened with, which its eID repeats. Usually
    /// the osisID, but not always: CrossWire's modules write
    /// sID="Gen.1.1.seID.00002", and an eID is matched against what was opened.
    QString startId;
    /// The further ids of an osisID list, folded into this verse.
    QStringList joined;
    std::optional<QString> altNumber;
};

struct PendingTitle
{
    QString type;
    bool canonical = false;
    QString text;
    QList<SourceNote> notes;
};

} // namespace

SourceDocument parseOsis(const QString &rawOsis, const ParseOptions &options)
{
    if (forbiddenDeclarations().match(rawOsis).hasMatch()) {
        throw OsisError(QStringLiteral(
            "OSIS files containing DTD or ENTITY declarations are not allowed."));
    }

    SourceDocument document;
    document.id = options.id;
    document.name = options.name;
    document.role = options.role;
    document.rawOsis = rawOsis;
    document.metadata.title = options.name;

    QList<SourceTitle> titles;
    QList<SourceMilestone> milestones;
    QStringList warnings;

    QStringList elementStack;
    QStringList divTypes;
    /// The nesting level of an open `div type="introduction"`, or 0 when none
    /// is open. What says which `</div>` ends the preamble it holds.
    int introductionDepth = 0;
    std::optional<PendingVerse> currentVerse;
    std::optional<PendingTitle> currentTitle;
    std::optional<QString> currentBook;
    std::optional<int> currentChapter;
    /// Titles seen while no chapter is open; they introduce the next one.
    QList<int> awaitingChapter;
    QList<int> verseMilestones;

    QString noteText;
    QString noteNumber;
    int noteOffset = 0;
    QString metadataField;
    QString metadataText;
    QString identifierType;
    /// How many header <work>s have opened, so the second one — which
    /// describes the versification — cannot overwrite the text's own fields.
    int worksSeen = 0;
    bool warnedJoined = false;
    bool warnedStrayText = false;

    const auto addWarning = [&warnings](const QString &warning) {
        if (!warnings.contains(warning)) {
            warnings.append(warning);
        }
    };

    const auto finishVerse = [&] {
        if (!currentVerse) {
            return;
        }
        if (document.hasVerse(currentVerse->reference.id)) {
            throw OsisError(QStringLiteral("Duplicate verse ID: %1")
                                .arg(currentVerse->reference.id));
        }

        const QString rawText = currentVerse->text;
        const QString text = collapseWhitespace(rawText);

        QList<SourceNote> adjustedNotes = currentVerse->notes;
        for (SourceNote &note : adjustedNotes) {
            note.charOffset = collapsedPrefixLength(rawText, note.charOffset);
        }
        // Milestone offsets were taken against the raw text and need the same
        // whitespace adjustment as notes.
        for (const int index : verseMilestones) {
            milestones[index].charOffset =
                collapsedPrefixLength(rawText, milestones[index].charOffset);
        }
        verseMilestones.clear();

        SourceVerse verse;
        verse.reference = currentVerse->reference;
        verse.label = currentVerse->label;
        verse.text = text;
        // A translation's brackets and stops belong to the word they touch; the
        // manuscripts keep theirs apart, which is how their readings are
        // compared and how their columns are counted.
        verse.tokens = tokenize(
            currentVerse->reference.id,
            text,
            adjustedNotes,
            options.role == SourceRole::Translation ? TokenStyle::Attached
                                                    : TokenStyle::Separate);
        verse.altNumber = currentVerse->altNumber;
        document.appendVerse(verse);
        for (const QString &joined : currentVerse->joined) {
            if (joined != verse.reference.id && !document.hasVerse(joined)) {
                document.joinedVerses.insert(joined, verse.reference.id);
            }
        }

        currentVerse.reset();
    };

    const auto finishTitle = [&] {
        if (!currentTitle) {
            return;
        }
        const QString text = collapseWhitespace(currentTitle->text);
        if (!text.isEmpty()) {
            const int titleIndex = int(titles.size());

            SourceTitle title;
            title.id = QStringLiteral("title-%1").arg(titleIndex);
            title.type = currentTitle->type;
            title.canonical = currentTitle->canonical;
            title.text = text;
            title.book = currentBook;
            title.chapter = currentChapter;
            for (int index = 0; index < currentTitle->notes.size(); ++index) {
                SourceNote note = currentTitle->notes.at(index);
                note.id = QStringLiteral("title-%1:n%2").arg(titleIndex).arg(index);
                note.tokenIndex = 0;
                title.notes.append(note);
            }
            titles.append(title);

            // A heading printed between chapters introduces the next one. A
            // heading inside front matter titles the book instead, so it keeps
            // no chapter.
            const QString enclosing = divTypes.isEmpty() ? QString() : divTypes.last();
            const bool frontMatter =
                enclosing == QLatin1String("introduction")
                || enclosing == QLatin1String("titlePage")
                || enclosing == QLatin1String("preface");
            if (!currentChapter.has_value() && !frontMatter) {
                awaitingChapter.append(titleIndex);
            }
        }
        currentTitle.reset();
    };

    QXmlStreamReader reader(rawOsis);

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType tokenType = reader.readNext();

        if (tokenType == QXmlStreamReader::DTD) {
            throw OsisError(QStringLiteral(
                "OSIS files containing DTD or ENTITY declarations are not allowed."));
        }

        if (tokenType == QXmlStreamReader::StartElement) {
            const QString local = reader.name().toString();
            const QXmlStreamAttributes attributes = reader.attributes();
            // Text an editor supplied where the witness has none is not the
            // witness's reading, and collating it as one would credit the
            // manuscript with words it lost. Kept out of the verse, whether
            // written as OSIS (seg type="x-supplied") or as TEI (<supplied>).
            const bool supplied = local == QLatin1String("supplied")
                || (local == QLatin1String("seg")
                    && attributeValue(attributes, QLatin1String("type"))
                        == QLatin1String("x-supplied"));
            elementStack.append(supplied ? QStringLiteral("#supplied") : local);

            if (local == QLatin1String("osis")
                && reader.namespaceUri() != QLatin1String(kOsisNamespace)) {
                // Read all the same: an OSIS 1.x file, or one written without
                // its namespace, still has verses worth having.
                addWarning(QStringLiteral("The file is not in the OSIS namespace (%1); it was "
                                          "read as OSIS anyway.")
                               .arg(reader.namespaceUri().isEmpty()
                                        ? QStringLiteral("none")
                                        : reader.namespaceUri().toString()));
            }

            if (local == QLatin1String("work") && elementStack.contains(QLatin1String("header"))) {
                ++worksSeen;
            }

            if (local == QLatin1String("osisText")) {
                document.metadata.workId =
                    attributeValue(attributes, QLatin1String("osisIDWork"));
                document.metadata.language =
                    attributeValue(attributes, QLatin1String("lang"));
            }

            if ((local == QLatin1String("title")
                 || local == QLatin1String("language")
                 || local == QLatin1String("scope")
                 || local == QLatin1String("rights")
                 || local == QLatin1String("refSystem")
                 || local == QLatin1String("identifier"))
                && elementStack.contains(QLatin1String("header"))) {
                metadataField = local;
                metadataText.clear();
                identifierType = attributeValue(attributes, QLatin1String("type"));
                if (identifierType.isEmpty()) {
                    identifierType = QStringLiteral("unspecified");
                }
            }

            if (local == QLatin1String("div")) {
                const QString divType = attributeValue(attributes, QLatin1String("type"));
                divTypes.append(divType);
                if (divType == QLatin1String("book")) {
                    const QString osisId =
                        attributeValue(attributes, QLatin1String("osisID"));
                    if (!osisId.isEmpty()) {
                        currentBook = osisId;
                    }
                }
                // Matter standing before verse 1 — an incipit, a
                // superscription, the scribe's heading to a chapter. OSIS has
                // no verse to put it in, so Milah writes it as this and reads
                // it back as the chapter's verse 0, which is what a transcriber
                // typed and what everything inside Milah keys it by.
                const QString introduces =
                    attributeValue(attributes, QLatin1String("osisRef"));
                // A chapter's, that is: "Gen.1". An introduction to a whole
                // book ("Gen") is editorial matter with no verse to key it by.
                static const QRegularExpression chapterRef(
                    QStringLiteral("^[^.\\s]+\\.\\d+$"));
                const QString chapter = osisIdsOf(introduces).value(0);
                if (divType == QLatin1String("introduction") && !introduces.isEmpty()
                    && !chapterRef.match(chapter).hasMatch()) {
                    addWarning(QStringLiteral("The introduction to %1 is not tied to a chapter "
                                              "and was not imported.")
                                   .arg(introduces));
                }
                if (divType == QLatin1String("introduction")
                    && chapterRef.match(chapter).hasMatch() && !currentVerse) {
                    // Its number from n where Milah wrote one, so that a second
                    // preamble ("0b", a second superscription) does not come
                    // back as a duplicate of the first.
                    const QString number = attributeValue(attributes, QLatin1String("n"));
                    PendingVerse verse;
                    verse.reference = parseReference(
                        chapter + QLatin1Char('.')
                        + (isPreamble(number) ? number : QStringLiteral("0")));
                    verse.label = verse.reference.verse;
                    // Closed by its own end tag rather than by a milestone.
                    verse.milestone = false;
                    currentVerse = verse;
                    // Which nesting level ends it, so a div inside it does not.
                    introductionDepth = int(divTypes.size());
                }
            }

            if (local == QLatin1String("chapter")) {
                const QString endId = attributeValue(attributes, QLatin1String("eID"));
                const QString osisId = attributeValue(attributes, QLatin1String("osisID"));
                if (!endId.isEmpty() && osisId.isEmpty()) {
                    currentChapter.reset();
                } else {
                    QString chapterId = osisId;
                    if (chapterId.isEmpty()) {
                        chapterId = attributeValue(attributes, QLatin1String("sID"));
                    }
                    const QStringList parts = chapterId.split(QLatin1Char('.'));
                    if (parts.size() >= 2 && isAllDigits(parts.at(1))) {
                        currentBook = parts.at(0);
                        currentChapter = parts.at(1).toInt();
                        for (const int index : awaitingChapter) {
                            titles[index].chapter = currentChapter;
                            titles[index].book = currentBook;
                        }
                        awaitingChapter.clear();
                    }
                }
            }

            // A heading outside the header belongs to the text: a manuscript
            // incipit, a chapter title, or a division heading. It is not part
            // of any verse.
            if (local == QLatin1String("title")
                && !elementStack.contains(QLatin1String("header"))
                && !currentVerse) {
                PendingTitle title;
                title.type = attributeValue(attributes, QLatin1String("type"));
                if (title.type.isEmpty()) {
                    title.type = QStringLiteral("main");
                }
                title.canonical =
                    attributeValue(attributes, QLatin1String("canonical"))
                    == QLatin1String("true");
                currentTitle = title;
                continue;
            }

            // TEI's <gap/> is OSIS's lacuna milestone under another name.
            if (local == QLatin1String("milestone") || local == QLatin1String("gap")) {
                const QString type = local == QLatin1String("gap")
                    ? QString::fromLatin1(kLacunaMilestone)
                    : attributeValue(attributes, QLatin1String("type"));
                if (!type.isEmpty()) {
                    SourceMilestone milestone;
                    milestone.id = QStringLiteral("milestone-%1").arg(milestones.size());
                    milestone.type = type;
                    milestone.n = attributeValue(attributes, QLatin1String("n"));
                    if (currentVerse) {
                        milestone.verseId = currentVerse->reference.id;
                        milestone.charOffset = int(currentVerse->text.size());
                    } else {
                        milestone.charOffset = 0;
                    }
                    milestones.append(milestone);
                    if (currentVerse) {
                        verseMilestones.append(int(milestones.size()) - 1);
                    }
                }
                continue;
            }

            if (local == QLatin1String("verse")) {
                const QString endId = attributeValue(attributes, QLatin1String("eID"));
                if (!endId.isEmpty()) {
                    if (!currentVerse || !currentVerse->milestone
                        || (endId != currentVerse->startId
                            && osisIdsOf(endId).value(0) != currentVerse->reference.id)) {
                        throw OsisError(
                            QStringLiteral("Unbalanced verse milestone: %1").arg(endId));
                    }
                    finishVerse();
                    continue;
                }

                const QString startId = attributeValue(attributes, QLatin1String("sID"));
                QString id = attributeValue(attributes, QLatin1String("osisID"));
                if (id.isEmpty()) {
                    id = startId;
                }
                if (id.isEmpty()) {
                    throw OsisError(QStringLiteral("Verse is missing osisID or sID."));
                }
                if (currentVerse) {
                    throw OsisError(
                        QStringLiteral("Verse %1 begins before the previous verse ends.")
                            .arg(id));
                }

                PendingVerse verse;
                verse.reference = parseReference(id);
                verse.label = attributeValue(attributes, QLatin1String("n"));
                if (verse.label.isEmpty()) {
                    verse.label = verse.reference.verse;
                }
                verse.milestone = !startId.isEmpty();
                verse.startId = startId;
                const QStringList ids = osisIdsOf(id);
                if (ids.size() > 1) {
                    verse.joined = ids.mid(1);
                    if (!warnedJoined) {
                        warnedJoined = true;
                        addWarning(QStringLiteral("Some verses are written together (%1, for "
                                                  "one); their text is kept under the first.")
                                       .arg(ids.join(QLatin1Char(' '))));
                    }
                }
                const QRegularExpressionMatch subType = altNumberPattern().match(
                    attributeValue(attributes, QLatin1String("subType")));
                if (subType.hasMatch()) {
                    verse.altNumber = subType.captured(1);
                }
                currentVerse = verse;
                continue;
            }

            if (local == QLatin1String("note")) {
                noteText.clear();
                noteNumber = attributeValue(attributes, QLatin1String("n"));
                if (currentVerse) {
                    noteOffset = int(currentVerse->text.size());
                } else if (currentTitle) {
                    noteOffset = int(currentTitle->text.size());
                } else {
                    noteOffset = 0;
                }
                continue;
            }

            if (currentVerse && local == QLatin1String("lb")) {
                currentVerse->text += QLatin1Char(' ');
            } else if (currentVerse && !supportedInline().contains(local)) {
                addWarning(QStringLiteral("Unsupported inline <%1> markup was flattened.")
                               .arg(local));
            }

            continue;
        }

        if (tokenType == QXmlStreamReader::Characters) {
            const QString text = reader.text().toString();
            if (!metadataField.isEmpty()) {
                metadataText += text;
                continue;
            }
            if (elementStack.contains(QStringLiteral("#supplied"))) {
                continue;
            }
            // Words between verses, or a note standing outside any verse or
            // heading, have nowhere to go in Milah's model. Dropped as before,
            // but said, so a file that loses a paragraph does not do so quietly.
            const bool stray = !currentVerse && !currentTitle
                && !elementStack.contains(QLatin1String("header"))
                && !text.trimmed().isEmpty();
            if (stray && !warnedStrayText) {
                warnedStrayText = true;
                addWarning(QStringLiteral("Text outside any verse was not imported, "
                                          "beginning “%1”.")
                               .arg(collapseWhitespace(text).left(60)));
            }
            if (elementStack.contains(QLatin1String("note"))) {
                if (currentVerse || currentTitle) {
                    noteText += text;
                }
                continue;
            }
            if (currentVerse) {
                currentVerse->text += text;
            } else if (currentTitle) {
                currentTitle->text += text;
            }
            continue;
        }

        if (tokenType == QXmlStreamReader::EndElement) {
            const QString local = reader.name().toString();

            if (local == QLatin1String("note") && (currentVerse || currentTitle)) {
                SourceNote note;
                note.number = noteNumber;
                note.text = collapseWhitespace(noteText);
                note.charOffset = noteOffset;
                if (currentVerse) {
                    note.id = QStringLiteral("%1:n%2")
                                  .arg(currentVerse->reference.id)
                                  .arg(currentVerse->notes.size());
                    currentVerse->notes.append(note);
                } else {
                    currentTitle->notes.append(note);
                }
                noteText.clear();
            }

            if (!metadataField.isEmpty() && metadataField == local) {
                const QString value = collapseWhitespace(metadataText);
                if (local == QLatin1String("identifier")) {
                    if (!value.isEmpty()
                        && !document.metadata.identifiers.contains(identifierType)) {
                        document.metadata.identifiers.insert(identifierType, value);
                    }
                } else if (local == QLatin1String("title") && !value.isEmpty()
                           && document.metadata.title == options.name) {
                    document.metadata.title = value;
                } else if (local == QLatin1String("language") && !value.isEmpty()
                           && worksSeen <= 1) {
                    // The first <work> only: the header carries a second one
                    // for the versification, whose language is not the text's.
                    document.metadata.language = value;
                } else if (local == QLatin1String("scope") && !value.isEmpty()
                           && worksSeen <= 1) {
                    document.metadata.scope = value;
                } else if (local == QLatin1String("refSystem") && !value.isEmpty()
                           && document.metadata.refSystem.isEmpty()) {
                    document.metadata.refSystem = value;
                } else if (local == QLatin1String("rights") && !value.isEmpty()) {
                    // Two of them, told apart by type: who holds the text, and
                    // what may be done with it. An untyped one is read as the
                    // copyright, which is what a header written before the
                    // split degrades to — one question answered rather than
                    // neither.
                    //
                    // First of each only. The header carries a second <work>
                    // for the versification system, and its terms — if it ever
                    // grows any — are not the terms of the text being loaded.
                    if (identifierType == QLatin1String("x-license")) {
                        if (document.metadata.license.isEmpty()) {
                            document.metadata.license = value;
                        }
                    } else if (document.metadata.rights.isEmpty()) {
                        document.metadata.rights = value;
                    }
                }
                metadataField.clear();
                metadataText.clear();
            }

            if (local == QLatin1String("verse") && currentVerse && !currentVerse->milestone) {
                finishVerse();
            }
            if (local == QLatin1String("title") && currentTitle) {
                finishTitle();
            }
            if (local == QLatin1String("div") && !divTypes.isEmpty()) {
                if (introductionDepth > 0 && int(divTypes.size()) == introductionDepth) {
                    finishVerse();
                    introductionDepth = 0;
                }
                divTypes.removeLast();
            }
            if (!elementStack.isEmpty()) {
                elementStack.removeLast();
            }
            continue;
        }
    }

    if (reader.hasError()) {
        throw OsisError(QStringLiteral("%1 (line %2, column %3)")
                            .arg(reader.errorString())
                            .arg(reader.lineNumber())
                            .arg(reader.columnNumber()));
    }
    if (currentVerse) {
        throw OsisError(QStringLiteral("Verse milestone %1 was not closed.")
                            .arg(currentVerse->reference.id));
    }
    if (document.verses.isEmpty()) {
        throw OsisError(QStringLiteral("The OSIS file contains no usable verses."));
    }

    document.titles = titles;
    document.milestones = milestones;
    document.warnings = warnings;
    return document;
}

} // namespace milah
