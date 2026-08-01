#include "core/transcription.h"

#include "core/project.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace milah {
namespace {

const QLatin1String kFormat("milah-transcription");
constexpr int kVersion = 1;

/// Writes a string only when it says something.
///
/// Every field a transcriber can leave blank is left out rather than written
/// empty, which is what lets a field be added later and read back as absent by
/// a version that has never heard of it. The whole manifest is read the same
/// way on the other side, so the version number never has to move.
void put(QJsonObject &object, const QString &key, const QString &value)
{
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

QJsonObject metadataToJson(const TranscriptionMetadata &metadata)
{
    QJsonObject json;
    put(json, QStringLiteral("manuscriptName"), metadata.manuscriptName);
    put(json, QStringLiteral("transcriber"), metadata.transcriber);
    put(json, QStringLiteral("origin"), metadata.origin);
    put(json, QStringLiteral("libraryMark"), metadata.libraryMark);
    put(json, QStringLiteral("shelfmark"), metadata.shelfmark);
    put(json, QStringLiteral("date"), metadata.date);
    put(json, QStringLiteral("language"), metadata.language);
    put(json, QStringLiteral("notes"), metadata.notes);

    QJsonObject extra;
    for (auto it = metadata.extra.constBegin(); it != metadata.extra.constEnd(); ++it) {
        extra.insert(it.key(), it.value());
    }
    if (!extra.isEmpty()) {
        json.insert(QStringLiteral("extra"), extra);
    }
    return json;
}

TranscriptionMetadata metadataFromJson(const QJsonObject &json)
{
    TranscriptionMetadata metadata;
    metadata.manuscriptName = json.value(QStringLiteral("manuscriptName")).toString();
    metadata.transcriber = json.value(QStringLiteral("transcriber")).toString();
    metadata.origin = json.value(QStringLiteral("origin")).toString();
    metadata.libraryMark = json.value(QStringLiteral("libraryMark")).toString();
    metadata.shelfmark = json.value(QStringLiteral("shelfmark")).toString();
    metadata.date = json.value(QStringLiteral("date")).toString();
    metadata.language = json.value(QStringLiteral("language")).toString();
    metadata.notes = json.value(QStringLiteral("notes")).toString();

    const QJsonObject extra = json.value(QStringLiteral("extra")).toObject();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        metadata.extra.insert(it.key(), it.value().toString());
    }
    return metadata;
}

QJsonObject wordToJson(const TranscribedWord &word)
{
    QJsonObject json;
    put(json, QStringLiteral("hebrew"), word.hebrew);
    put(json, QStringLiteral("english"), word.english);
    if (word.englishIsOwn) {
        json.insert(QStringLiteral("englishIsOwn"), true);
    }
    return json;
}

TranscribedWord wordFromJson(const QJsonObject &json)
{
    TranscribedWord word;
    word.hebrew = json.value(QStringLiteral("hebrew")).toString();
    word.english = json.value(QStringLiteral("english")).toString();
    word.englishIsOwn = json.value(QStringLiteral("englishIsOwn")).toBool();
    return word;
}

QJsonObject verseToJson(const TranscribedVerse &verse)
{
    QJsonArray words;
    for (const TranscribedWord &word : verse.words) {
        words.append(wordToJson(word));
    }

    QJsonObject json;
    put(json, QStringLiteral("number"), verse.number);
    if (verse.startsNewChapter) {
        json.insert(QStringLiteral("startsNewChapter"), true);
    }
    if (!words.isEmpty()) {
        json.insert(QStringLiteral("words"), words);
    }
    return json;
}

TranscribedVerse verseFromJson(const QJsonObject &json)
{
    TranscribedVerse verse;
    verse.number = json.value(QStringLiteral("number")).toString();
    verse.startsNewChapter = json.value(QStringLiteral("startsNewChapter")).toBool();
    const QJsonArray words = json.value(QStringLiteral("words")).toArray();
    verse.words.reserve(words.size());
    for (const QJsonValue &value : words) {
        verse.words.append(wordFromJson(value.toObject()));
    }
    return verse;
}

QJsonObject pageToJson(const TranscribedPage &page)
{
    QJsonArray verses;
    for (const TranscribedVerse &verse : page.verses) {
        verses.append(verseToJson(verse));
    }

    QJsonObject json;
    put(json, QStringLiteral("imageEntry"), page.imageEntry);
    put(json, QStringLiteral("imageName"), page.imageName);
    put(json, QStringLiteral("sourcePath"), page.sourcePath);
    put(json, QStringLiteral("book"), page.book);
    put(json, QStringLiteral("bookLabel"), page.bookLabel);
    json.insert(QStringLiteral("firstChapter"), page.firstChapter);
    if (!verses.isEmpty()) {
        json.insert(QStringLiteral("verses"), verses);
    }
    return json;
}

TranscribedPage pageFromJson(const QJsonObject &json)
{
    TranscribedPage page;
    page.imageEntry = json.value(QStringLiteral("imageEntry")).toString();
    page.imageName = json.value(QStringLiteral("imageName")).toString();
    page.sourcePath = json.value(QStringLiteral("sourcePath")).toString();
    page.book = json.value(QStringLiteral("book")).toString();
    // Absent from files written before the book could be named as well as
    // abbreviated, which reads correctly as "the canonical name of the id".
    page.bookLabel = json.value(QStringLiteral("bookLabel")).toString();
    // A chapter is never zero, so a manifest that somehow says so is taken to
    // mean it did not say.
    page.firstChapter = std::max(1, json.value(QStringLiteral("firstChapter")).toInt(1));

    const QJsonArray verses = json.value(QStringLiteral("verses")).toArray();
    page.verses.reserve(verses.size());
    for (const QJsonValue &value : verses) {
        page.verses.append(verseFromJson(value.toObject()));
    }
    return page;
}

} // namespace

int chapterOfVerse(const TranscribedPage &page, int verseIndex)
{
    int chapter = page.firstChapter;
    for (int index = 0; index <= verseIndex && index < page.verses.size(); ++index) {
        // The break belongs to the verse carrying it, so it counts at the verse
        // itself and not from the one after: "move this verse to a new chapter"
        // has to move this verse.
        if (page.verses.at(index).startsNewChapter) {
            ++chapter;
        }
    }
    return chapter;
}

QString transcribedVerseId(const TranscribedPage &page, int verseIndex)
{
    if (verseIndex < 0 || verseIndex >= page.verses.size()) {
        return QString();
    }
    const TranscribedVerse &verse = page.verses.at(verseIndex);
    if (page.book.isEmpty() || verse.number.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1.%2.%3")
        .arg(page.book)
        .arg(chapterOfVerse(page, verseIndex))
        .arg(verse.number);
}

bool looksLikeVerseNumber(const QString &text)
{
    // A trailing letter because a verse a manuscript divides is 12a and 12b,
    // and a transcriber typing what they see must be able to say so.
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+[a-zA-Z]?$"));
    return pattern.match(text.trimmed()).hasMatch();
}

MilahProjectPayload transcriptionPayload(
    const TranscriptionDocument &document,
    const QHash<QString, QByteArray> &imageBytes)
{
    QJsonArray pages;
    for (const TranscribedPage &page : document.pages) {
        pages.append(pageToJson(page));
    }

    MilahProjectPayload payload;
    payload.suggestedName = document.metadata.manuscriptName.isEmpty()
        ? QStringLiteral("Transcription.trscrpt")
        : QStringLiteral("%1.trscrpt").arg(document.metadata.manuscriptName);
    payload.manifest = QJsonObject{
        {QStringLiteral("format"), QString(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("savedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("metadata"), metadataToJson(document.metadata)},
        {QStringLiteral("pages"), pages},
    };

    // The folios travel with the text. A transcription read months later on
    // another machine has to show what was being read, and a path into someone
    // else's filesystem does not.
    for (const TranscribedPage &page : document.pages) {
        if (page.imageEntry.isEmpty()) {
            continue;
        }
        const auto bytes = imageBytes.constFind(page.imageEntry);
        if (bytes == imageBytes.constEnd()) {
            continue;
        }
        ProjectFile file;
        file.path = page.imageEntry;
        file.contentBase64 = QString::fromLatin1(bytes.value().toBase64());
        payload.files.append(file);
    }

    return payload;
}

TranscriptionDocument restoreTranscription(const MilahProjectPayload &payload)
{
    const QJsonObject &manifest = payload.manifest;

    // An edition and a transcription are both zip archives with a manifest, so
    // this is the one place that can tell the reader they have opened the wrong
    // kind of file rather than letting it fail somewhere much less clear.
    const QString format = manifest.value(QStringLiteral("format")).toString();
    if (format == QLatin1String("milah-project")) {
        throw ProjectError(QStringLiteral(
            "That is a Milah edition, not a transcription. Open it from the "
            "Textual criticism tab."));
    }
    if (format != kFormat || manifest.value(QStringLiteral("version")).toInt() != kVersion) {
        throw ProjectError(
            QStringLiteral("This transcription version is not supported."));
    }

    TranscriptionDocument document;
    document.metadata = metadataFromJson(manifest.value(QStringLiteral("metadata")).toObject());

    const QJsonArray pages = manifest.value(QStringLiteral("pages")).toArray();
    document.pages.reserve(pages.size());
    for (const QJsonValue &value : pages) {
        document.pages.append(pageFromJson(value.toObject()));
    }
    return document;
}

QHash<QString, QByteArray> transcriptionImages(const MilahProjectPayload &payload)
{
    QHash<QString, QByteArray> images;
    for (const ProjectFile &file : payload.files) {
        images.insert(file.path, QByteArray::fromBase64(file.contentBase64.toLatin1()));
    }
    return images;
}

} // namespace milah
