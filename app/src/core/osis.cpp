#include "core/osis.h"

#include "core/tokenize.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

namespace milah {
namespace {

const QSet<QString> &supportedInline()
{
    static const QSet<QString> tags = {
        QStringLiteral("verse"),        QStringLiteral("note"),
        QStringLiteral("w"),            QStringLiteral("seg"),
        QStringLiteral("hi"),           QStringLiteral("divineName"),
        QStringLiteral("name"),         QStringLiteral("foreign"),
        QStringLiteral("transChange"),  QStringLiteral("q"),
        QStringLiteral("reference"),    QStringLiteral("lb"),
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

VerseReference parseReference(const QString &id)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    static const QRegularExpression pattern(
        QStringLiteral("^([^.]+)\\.(\\d+)\\.(.+)$"));

    const QString first = id.split(whitespace).value(0);
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
            elementStack.append(local);
            const QXmlStreamAttributes attributes = reader.attributes();

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

            if (local == QLatin1String("milestone")) {
                const QString type = attributeValue(attributes, QLatin1String("type"));
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
                        || currentVerse->reference.id != endId) {
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
                } else if (local == QLatin1String("language") && !value.isEmpty()) {
                    document.metadata.language = value;
                } else if (local == QLatin1String("scope") && !value.isEmpty()) {
                    document.metadata.scope = value;
                } else if (local == QLatin1String("rights") && !value.isEmpty()
                           && document.metadata.rights.isEmpty()) {
                    // First only. The header carries a second <work> for the
                    // versification system, and its terms — if it ever grows
                    // any — are not the terms of the text being loaded.
                    document.metadata.rights = value;
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
