#pragma once

#include "core/types.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

namespace milah {

/// What a transcriber makes: a folio photographed, and the text read off it.
///
/// Deliberately not a `ProjectState`. An edition is several manuscripts already
/// in OSIS being reconciled against one another; a transcription is one pair of
/// eyes turning a photograph into text that does not exist yet. They share the
/// window and nothing else, and a document type that tried to be both would be
/// mostly empty fields whichever job was being done.

/// One word as it was read, and what it was taken to mean.
///
/// The marker — the Strong's number, the M, the D — is not here on purpose. It
/// is a function of the word, the shipped lexicons and the editor's own
/// dictionary, so it is derived at draw time by markerFor(). Storing it would
/// freeze a verdict that a new dictionary entry ought to change.
struct TranscribedWord
{
    /// As the transcriber typed it, pointing and all. Nothing is normalised on
    /// the way in: what the folio says is the record, and the lookups normalise
    /// for themselves.
    QString hebrew;
    /// The lexicon's suggestion unless the transcriber has overwritten it.
    QString english;
    /// True once the transcriber has edited the gloss themselves, after which
    /// re-reading the Hebrew word must not overwrite what they wrote.
    bool englishIsOwn = false;
};

struct TranscribedVerse
{
    /// As typed: usually "3", but "12a" is a verse number too. Never an int —
    /// verse numbers are strings everywhere else in Milah for the same reason.
    QString number;
    /// This verse opens a new chapter. See chapterOfVerse().
    bool startsNewChapter = false;
    QList<TranscribedWord> words;
};

/// One folio, and everything read off it.
struct TranscribedPage
{
    /// Where the image sits inside the archive, e.g. "images/003-folio.jpg".
    /// Empty until the page has been saved once.
    QString imageEntry;
    /// The image file's own name, for the toolbar and the page list.
    QString imageName;
    /// Where the image was opened from. A hint for reopening a project whose
    /// folder has moved, never the thing the transcription depends on — the
    /// image itself travels inside the archive.
    QString sourcePath;
    /// The OSIS id of the book being transcribed — what transcribedVerseId()
    /// addresses the verses by, and what an exported file carries.
    QString book;
    /// The book as the transcriber wrote it, when that is not simply the
    /// canonical name of `book`.
    ///
    /// Held separately because the two stop being derivable from one another
    /// the moment the work is outside the canon: a transcriber reading Tobit
    /// writes "Tobit" and coins "Tob" themselves, and neither can be recovered
    /// from the other. Empty for a canonical book, where bookName(book) says it.
    QString bookLabel;
    /// The chapter this page opens in. Verses after a chapter break count on
    /// from here; see chapterOfVerse().
    int firstChapter = 1;
    QList<TranscribedVerse> verses;
};

/// What the folio is, as against what it says. All optional: a transcriber
/// filling this in is describing a manuscript, not satisfying a form.
struct TranscriptionMetadata
{
    QString manuscriptName;
    QString transcriber;
    QString origin;
    QString libraryMark;
    QString shelfmark;
    QString date;
    QString language;
    QString notes;
    /// Anything the fields above do not cover, so a project can carry a note
    /// the next version of Milah has a proper field for without losing it in
    /// the meantime.
    QMap<QString, QString> extra;
};

struct TranscriptionDocument
{
    TranscriptionMetadata metadata;
    QList<TranscribedPage> pages;

    bool isEmpty() const { return pages.isEmpty(); }
};

/// The chapter a verse falls in: the page's opening chapter, plus one for every
/// chapter break at or before it.
///
/// Stored as a flag on the verse rather than a number, because "move to a new
/// chapter" means this verse *and every verse after it*, and a flag says that
/// once instead of rewriting every number below it. It also makes undoing the
/// move a single bit, and moving the break somewhere else a matter of clearing
/// one flag and setting another rather than renumbering twice.
int chapterOfVerse(const TranscribedPage &page, int verseIndex);

/// The verse id a transcribed verse would carry in OSIS: "Book.Chapter.Verse",
/// the same shape the rest of Milah keys verses by. Empty when the page has no
/// book or the verse has no number, because half an id is worse than none.
QString transcribedVerseId(const TranscribedPage &page, int verseIndex);

/// True when `text` is a verse number rather than a word — digits, optionally
/// with the letter a manuscript splits a verse with.
bool looksLikeVerseNumber(const QString &text);

/// Packs a transcription and its folio images into the payload a `.trscrpt`
/// archive is written from. `imageBytes` is keyed by `TranscribedPage::
/// imageEntry`; a page whose bytes are missing keeps its text and loses only
/// the picture, which is the right way round.
MilahProjectPayload transcriptionPayload(
    const TranscriptionDocument &document,
    const QHash<QString, QByteArray> &imageBytes);

/// Rebuilds a transcription from a payload. Throws ProjectError when the
/// manifest is not a transcription — an edition and a transcription are both
/// zip archives with a manifest, and opening one as the other would otherwise
/// fail somewhere much less clear.
TranscriptionDocument restoreTranscription(const MilahProjectPayload &payload);

/// The folio images carried by a payload, keyed by entry path, ready to be
/// handed straight back to transcriptionPayload() when it is saved again.
QHash<QString, QByteArray> transcriptionImages(const MilahProjectPayload &payload);

} // namespace milah
