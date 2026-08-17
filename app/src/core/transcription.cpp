#include "core/transcription.h"

#include "core/books.h"
#include "core/project.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace milah {
namespace {

const QLatin1String kFormat("milah-transcription");
constexpr int kVersion = 1;

/// Room for a shelfmark, a folio label and an extension together, and no more.
/// A scan's id is a remote string — MILAH_MANUSCRIPT_URL points Milah at any
/// catalogue an institution cares to serve — so its length is not this
/// program's to trust. The bound is about what happens when someone unzips a
/// transcription by hand: an entry longer than a filesystem will take extracts
/// as an error rather than as a folio.
constexpr int kMaxNameFragment = 96;

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
    put(json, QStringLiteral("folios"), metadata.folios);
    put(json, QStringLiteral("date"), metadata.date);
    put(json, QStringLiteral("material"), metadata.material);
    put(json, QStringLiteral("provenance"), metadata.provenance);
    put(json, QStringLiteral("translatedFrom"), metadata.translatedFrom);
    put(json,
        QStringLiteral("translatedFromCertainty"),
        metadata.translatedFromCertainty);
    put(json, QStringLiteral("exemplar"), metadata.exemplar);
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
    metadata.folios = json.value(QStringLiteral("folios")).toString();
    metadata.date = json.value(QStringLiteral("date")).toString();
    metadata.material = json.value(QStringLiteral("material")).toString();
    metadata.provenance = json.value(QStringLiteral("provenance")).toString();
    metadata.translatedFrom = json.value(QStringLiteral("translatedFrom")).toString();
    metadata.translatedFromCertainty =
        json.value(QStringLiteral("translatedFromCertainty")).toString();
    metadata.exemplar = json.value(QStringLiteral("exemplar")).toString();
    metadata.language = json.value(QStringLiteral("language")).toString();
    metadata.notes = json.value(QStringLiteral("notes")).toString();

    const QJsonObject extra = json.value(QStringLiteral("extra")).toObject();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        metadata.extra.insert(it.key(), it.value().toString());
    }
    return metadata;
}

/// A word's place on the folio, as `"x y w h"`.
///
/// One string rather than four numbers so that put() carries it like every
/// other optional field: a rectangle nobody has is simply absent, and a version
/// of Milah that predates recognition reads the file exactly as it always did.
QString boxToText(const QRect &box)
{
    if (box.isNull()) {
        return QString();
    }
    return QStringLiteral("%1 %2 %3 %4")
        .arg(box.x())
        .arg(box.y())
        .arg(box.width())
        .arg(box.height());
}

QRect boxFromText(const QString &text)
{
    if (text.isEmpty()) {
        return QRect();
    }
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList parts = text.split(whitespace, Qt::SkipEmptyParts);
    if (parts.size() != 4) {
        return QRect();
    }

    int numbers[4] = {0, 0, 0, 0};
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        numbers[index] = parts.at(index).toInt(&ok);
        // A box read only partly is a box in the wrong place, so a malformed
        // one becomes no box at all and the word keeps its text.
        if (!ok) {
            return QRect();
        }
    }
    return QRect(numbers[0], numbers[1], numbers[2], numbers[3]);
}

QJsonObject wordToJson(const TranscribedWord &word)
{
    QJsonObject json;
    put(json, QStringLiteral("hebrew"), word.hebrew);
    put(json, QStringLiteral("english"), word.english);
    if (word.englishIsOwn) {
        json.insert(QStringLiteral("englishIsOwn"), true);
    }
    put(json, QStringLiteral("note"), word.note);
    put(json, QStringLiteral("recognised"), word.recognised);
    put(json, QStringLiteral("box"), boxToText(word.box));
    if (word.line >= 0) {
        json.insert(QStringLiteral("line"), word.line);
    }
    if (word.marginal) {
        json.insert(QStringLiteral("marginal"), true);
    }
    if (word.endsLine) {
        json.insert(QStringLiteral("endsLine"), true);
    }
    if (word.unchecked) {
        json.insert(QStringLiteral("unchecked"), true);
    }
    return json;
}

TranscribedWord wordFromJson(const QJsonObject &json)
{
    TranscribedWord word;
    word.hebrew = json.value(QStringLiteral("hebrew")).toString();
    word.english = json.value(QStringLiteral("english")).toString();
    word.englishIsOwn = json.value(QStringLiteral("englishIsOwn")).toBool();
    // Absent from files written before a word could be remarked on, which reads
    // correctly as nothing having been said about it.
    word.note = json.value(QStringLiteral("note")).toString();
    // Both absent from files written before a machine could read a folio, and
    // both mean the right thing when absent: nobody knows where this word is on
    // the picture, and a person typed it.
    word.box = boxFromText(json.value(QStringLiteral("box")).toString());
    // -1 where it is absent, which is right for every word in every file
    // written before Milah kept the line: nothing knows which line it was on,
    // and the training export says so by leaving it out rather than guessing.
    word.line = json.value(QStringLiteral("line")).toInt(-1);
    word.endsLine = json.value(QStringLiteral("endsLine")).toBool();
    // Absent from every file written before a box could be held out of the
    // work, which reads correctly as "all of this is the text".
    word.marginal = json.value(QStringLiteral("marginal")).toBool();
    // Absent from every folio read before the machine's own reading was kept.
    // Empty reads as "there is nothing to go back to", which is the truth.
    word.recognised = json.value(QStringLiteral("recognised")).toString();
    word.unchecked = json.value(QStringLiteral("unchecked")).toBool();
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
    put(json, QStringLiteral("imageUrl"), page.imageUrl);
    put(json, QStringLiteral("imageLabel"), page.imageLabel);
    put(json, QStringLiteral("book"), page.book);
    put(json, QStringLiteral("bookLabel"), page.bookLabel);
    json.insert(QStringLiteral("firstChapter"), page.firstChapter);
    // Both or neither: a verse with no word count says where to resume without
    // saying how far in, which is worse than saying nothing.
    if (!page.fillEndVerse.isEmpty() && page.fillEndWord >= 0) {
        json.insert(QStringLiteral("fillEndVerse"), page.fillEndVerse);
        json.insert(QStringLiteral("fillEndWord"), page.fillEndWord);
    }
    if (page.fillStartLine >= 0) {
        json.insert(QStringLiteral("fillStartLine"), page.fillStartLine);
    }
    // Both or neither, for the same reason the end pair is: a verse with no word
    // count says where the pour began without saying how far in.
    if (!page.fillStartVerse.isEmpty() && page.fillStartWord >= 0) {
        json.insert(QStringLiteral("fillStartVerse"), page.fillStartVerse);
        json.insert(QStringLiteral("fillStartWord"), page.fillStartWord);
    }
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
    // Absent from files written before a folio could come from a library, which
    // reads correctly as "this one came off a disk".
    page.imageUrl = json.value(QStringLiteral("imageUrl")).toString();
    page.imageLabel = json.value(QStringLiteral("imageLabel")).toString();
    page.book = json.value(QStringLiteral("book")).toString();
    // Absent from files written before the book could be named as well as
    // abbreviated, which reads correctly as "the canonical name of the id".
    page.bookLabel = json.value(QStringLiteral("bookLabel")).toString();
    // A chapter is never zero, so a manifest that somehow says so is taken to
    // mean it did not say.
    page.firstChapter = std::max(1, json.value(QStringLiteral("firstChapter")).toInt(1));
    // Absent from every file written before a folio could be filled from a
    // published transcription, which reads correctly as "none has run here".
    page.fillEndVerse = json.value(QStringLiteral("fillEndVerse")).toString();
    page.fillEndWord = json.value(QStringLiteral("fillEndWord")).toInt(-1);
    page.fillStartLine = json.value(QStringLiteral("fillStartLine")).toInt(-1);
    page.fillStartVerse = json.value(QStringLiteral("fillStartVerse")).toString();
    page.fillStartWord = json.value(QStringLiteral("fillStartWord")).toInt(-1);

    const QJsonArray verses = json.value(QStringLiteral("verses")).toArray();
    page.verses.reserve(verses.size());
    for (const QJsonValue &value : verses) {
        page.verses.append(verseFromJson(value.toObject()));
    }
    return page;
}

/// Letters and digits, joined up: "Ebr. 530" becomes "Ebr530".
///
/// Stricter than libraryFileName's rule, which keeps dots and dashes, and
/// deliberately so rather than by oversight. That name is a machine's key in
/// the published library, where staying close to what the transcriber typed
/// matters more than reading well. This one is offered to a person in a Save
/// dialog, where "LUK_Ebr.530.trscrpt" reads as a file with two extensions.
QString condensedName(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            out.append(character);
        }
    }
    return out;
}

} // namespace

ResumePoint resumeFill(const TranscriptionDocument &document, int page)
{
    // Backwards from the folio before this one, stopping at the first leaf that
    // holds any text rather than running on to the earliest — a verso left blank
    // or a folio skipped for later must not send the next leaf back two places
    // in the book.
    for (int index = std::min(page, int(document.pages.size())) - 1; index >= 0; --index) {
        const TranscribedPage &earlier = document.pages.at(index);

        // The last verse with words in it. Trailing empties are ordinary: a
        // folio always keeps a blank verse at the end for typing into.
        int last = -1;
        for (int verse = earlier.verses.size() - 1; verse >= 0; --verse) {
            if (!earlier.verses.at(verse).words.isEmpty()) {
                last = verse;
                break;
            }
        }
        if (last < 0) {
            continue;
        }

        if (isPreamble(earlier.verses.at(last).number)) {
            // An incipit or a scribe's heading to a chapter. It is on the leaf,
            // but it is not a verse of the source and so says nothing about
            // where in the source the reading had got to.
            continue;
        }
        const QString id = transcribedVerseId(earlier, last);
        if (id.isEmpty()) {
            // Words but no verse number, or a folio with no book: there is text
            // here, but nothing that says where in the source it sits.
            continue;
        }
        return ResumePoint{id, int(earlier.verses.at(last).words.size())};
    }
    return ResumePoint{};
}

TranscriptionDocument withoutMarginalia(const TranscriptionDocument &document)
{
    TranscriptionDocument out = document;
    for (TranscribedPage &page : out.pages) {
        // What each line's marginalia say, in the order they were read.
        QMap<int, QStringList> notes;
        for (const TranscribedVerse &verse : page.verses) {
            for (const TranscribedWord &word : verse.words) {
                if (word.marginal && !word.hebrew.isEmpty()) {
                    notes[word.line].append(word.hebrew);
                }
            }
        }
        if (notes.isEmpty()) {
            continue;
        }

        // Out of the text, remembering where each line's last remaining word
        // ended up so a note has something to hang on. Positions rather than
        // pointers: the lists below are being rebuilt as this goes.
        QMap<int, QPair<int, int>> anchors;
        for (int index = 0; index < page.verses.size(); ++index) {
            QList<TranscribedWord> kept;
            for (const TranscribedWord &word : page.verses.at(index).words) {
                if (word.marginal) {
                    continue;
                }
                kept.append(word);
                if (word.line >= 0) {
                    anchors.insert(word.line, {index, int(kept.size()) - 1});
                }
            }
            page.verses[index].words = kept;
        }

        for (auto line = notes.constBegin(); line != notes.constEnd(); ++line) {
            // Its own line first. A note the recogniser gave a line of its own —
            // which is most of them, since marginalia sit beside the text block
            // rather than inside it — has no text on that line to attach to, so
            // it falls back to the nearest line above, which is the part of the
            // text it stands next to. Failing that, the nearest below.
            QPair<int, int> place{-1, -1};
            if (anchors.contains(line.key())) {
                place = anchors.value(line.key());
            } else {
                for (auto above = anchors.constBegin(); above != anchors.constEnd(); ++above) {
                    if (above.key() < line.key()) {
                        place = above.value();
                    }
                }
                if (place.first < 0 && !anchors.isEmpty()) {
                    place = anchors.constBegin().value();
                }
            }
            if (place.first < 0) {
                // A folio with nothing on it but marginalia. There is no word
                // for a note to be a note on, and inventing one would put text
                // into the export that the transcriber never wrote.
                continue;
            }

            TranscribedWord &word = page.verses[place.first].words[place.second];
            QStringList all = line.value();
            if (!word.note.isEmpty()) {
                all.prepend(word.note);
            }
            word.note = all.join(QStringLiteral("; "));
        }
    }
    return out;
}

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

bool isPreamble(const QString &verseNumber)
{
    if (!looksLikeVerseNumber(verseNumber)) {
        return false;
    }
    // Every digit a zero, so "0" and "00" both count — and a trailing letter is
    // allowed through the same way it is for any other number, because a
    // preamble a manuscript divides into 0a and 0b is still a preamble.
    for (const QChar character : verseNumber.trimmed()) {
        if (character.isDigit() && character != QLatin1Char('0')) {
            return false;
        }
    }
    return true;
}

bool isUntouched(const TranscribedPage &page)
{
    for (const TranscribedVerse &verse : page.verses) {
        if (!verse.number.isEmpty()) {
            return false;
        }
        for (const TranscribedWord &word : verse.words) {
            if (!word.hebrew.isEmpty()) {
                return false;
            }
        }
    }
    return true;
}

bool isUntouched(const TranscriptionDocument &document)
{
    for (const TranscribedPage &page : document.pages) {
        if (!isUntouched(page)) {
            return false;
        }
    }
    return true;
}

QString archiveNameFragment(const QString &text)
{
    // ASCII only, and not merely because of the slash: an entry name is read
    // back by whatever tool the transcriber unzips the file with, and one made
    // of letters, digits, dot, dash and underscore is one every tool agrees on.
    static const QRegularExpression unusable(QStringLiteral("[^A-Za-z0-9._-]+"));
    QString fragment = text;
    fragment.replace(unusable, QStringLiteral("_"));

    // The end rather than the beginning, because the end is what tells one
    // folio from the next: the folio number is the last thing the name carries,
    // and an address's distinctive part is its tail as well.
    if (fragment.size() > kMaxNameFragment) {
        fragment = fragment.right(kMaxNameFragment);
    }

    // A name opening with a dot extracts as a hidden file, and a run of them is
    // how a path climbs out of a directory. Trailing ones are noise, and a cut
    // through a run of punctuation is exactly how one gets left behind.
    static const QRegularExpression edges(QStringLiteral("^[._-]+|[._-]+$"));
    fragment.remove(edges);
    return fragment;
}

QString imageEntryFor(int pageIndex, const QString &name)
{
    const QString fragment = archiveNameFragment(name);
    // Numbered by page, because two folios called `1.jpg` out of different
    // folders would otherwise be the same entry and the second would silently
    // replace the first — and because the sanitised name cannot be asked to do
    // it: an id that is an address, and the same id with one separator changed,
    // come out of archiveNameFragment() identical.
    //
    // The number in front is also what makes ".." harmless: no entry is ever
    // that name, whatever the catalogue said.
    if (fragment.isEmpty()) {
        // Nothing in the name survived — an id written entirely in Hebrew, say.
        // The folio's place still names it, which is the part that matters.
        return QStringLiteral("images/%1").arg(pageIndex + 1, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("images/%1-%2")
        .arg(pageIndex + 1, 3, 10, QLatin1Char('0'))
        .arg(fragment);
}

QList<TranscribedVerse> parseTranscribedText(const QString &text)
{
    QList<TranscribedVerse> verses;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);

    for (const QString &token : tokens) {
        if (looksLikeVerseNumber(token)) {
            TranscribedVerse verse;
            verse.number = token.trimmed();
            verses.append(verse);
            continue;
        }
        if (verses.isEmpty()) {
            // Words before any number. A fragment cut out of the middle of a
            // chapter begins mid-verse, and refusing it because it does not
            // open with a number would be refusing the commonest paste there is.
            verses.append(TranscribedVerse());
        }
        TranscribedWord word;
        word.hebrew = token;
        verses.last().words.append(word);
    }

    return verses;
}

QString verseHeading(const TranscribedPage &page, int verseIndex)
{
    if (verseIndex < 0 || verseIndex >= page.verses.size()) {
        return QString();
    }
    const QString number = page.verses.at(verseIndex).number;
    if (number.isEmpty()) {
        // Nothing has been read off it yet — but it is still a verse, and the
        // card still has to be headed something.
        return QStringLiteral("Unnumbered");
    }

    const int chapter = chapterOfVerse(page, verseIndex);
    if (isPreamble(number)) {
        // Named for what it is rather than as "4:0", which says nothing to a
        // transcriber who has not been told the convention.
        return page.book.isEmpty()
            ? QStringLiteral("%1 preamble").arg(chapter)
            : QStringLiteral("%1 %2 preamble").arg(page.book).arg(chapter);
    }
    if (page.book.isEmpty()) {
        // The chapter is known from the folio even when the book is not, and
        // half a reference is worth more than none while transcribing.
        return QStringLiteral("%1:%2").arg(chapter).arg(number);
    }
    return QStringLiteral("%1 %2:%3").arg(page.book).arg(chapter).arg(number);
}

QString libraryFileName(const QString &bookOsisId, const QString &manuscriptName)
{
    const QString &book = bookOsisId;

    QString name;
    name.reserve(manuscriptName.size());
    for (const QChar character : manuscriptName) {
        if (character.isLetterOrNumber() || character == QLatin1Char('.')
            || character == QLatin1Char('-')) {
            name.append(character);
        }
    }
    if (name.isEmpty()) {
        // Something has to name it, and a file called "REV__hebrew_commented"
        // reads as a mistake rather than as an unnamed manuscript.
        name = QStringLiteral("Transcription");
    }

    return QStringLiteral("%1_%2_hebrew_commented.osis")
        .arg(book.isEmpty() ? QStringLiteral("NT") : book, name);
}

QString transcriptionFileStem(const TranscriptionDocument &document, int pageIndex)
{
    // The folio on screen names the book, and where it does not — a cover, a
    // flyleaf, a page opened before the transcriber said what they were reading
    // — the first page that names one does. A transcription knows what it is
    // even while sitting on a blank leaf.
    QString book;
    if (pageIndex >= 0 && pageIndex < document.pages.size()) {
        book = document.pages.at(pageIndex).book;
    }
    if (book.isEmpty()) {
        for (const TranscribedPage &page : document.pages) {
            if (!page.book.isEmpty()) {
                book = page.book;
                break;
            }
        }
    }

    // The shelfmark first. It is what makes one copy of Luke a different thing
    // from another, and it is what a transcriber writes on the folder. The
    // manuscript name is the fallback and not the other way round, because for
    // a transcription of one book it is usually the *book's* name — so leading
    // with it would give "LUK_Luke", which distinguishes nothing.
    QString manuscript = condensedName(document.metadata.shelfmark);
    if (manuscript.isEmpty()) {
        manuscript = condensedName(document.metadata.manuscriptName);
    }

    const QString id = condensedName(book);
    if (!id.isEmpty() && !manuscript.isEmpty()) {
        return id + QLatin1Char('_') + manuscript;
    }
    if (!id.isEmpty()) {
        return id;
    }
    if (!manuscript.isEmpty()) {
        return manuscript;
    }
    // A transcription that has said nothing about itself yet still has to be
    // offered a name.
    return QStringLiteral("Transcription");
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
    // The same name the Save dialog offers. No folio is on screen from in here,
    // so the book comes from the first page that names one.
    payload.suggestedName =
        QStringLiteral("%1.trscrpt").arg(transcriptionFileStem(document, -1));
    payload.manifest = QJsonObject{
        {QStringLiteral("format"), QString(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("savedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("metadata"), metadataToJson(document.metadata)},
        {QStringLiteral("pages"), pages},
    };
    // Only when there is one, so a transcription nobody has filled does not
    // carry an empty key saying it was.
    if (!document.fillSource.isEmpty()) {
        payload.manifest.insert(QStringLiteral("fillSource"), document.fillSource);
    }

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
    // Absent from every file written before the fill remembered its source,
    // which reads correctly as "ask for it once".
    document.fillSource = manifest.value(QStringLiteral("fillSource")).toString();

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
