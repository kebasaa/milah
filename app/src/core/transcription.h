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
    /// The transcriber's own remark on this word — a doubtful letter, a
    /// correction the scribe made, anything the text alone cannot carry.
    ///
    /// Exported as an OSIS note anchored at the word, in the same markup the
    /// comparison's notes use.
    QString note;
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
    /// Where the image was opened from on this machine. A hint for reopening a
    /// project whose folder has moved, never the thing the transcription
    /// depends on. Empty for a folio that came from a library.
    QString sourcePath;
    /// Where the folio can be fetched from again, for one that came from a
    /// published scan. Empty for a local image.
    ///
    /// A scan's folios are linked rather than copied: a codex runs to hundreds
    /// of leaves, and carrying every one of them inside the file would make a
    /// transcription of one page weigh as much as the whole manuscript. What is
    /// kept instead is the address, and the bytes only for the folios actually
    /// worked on — see TranscriptionController.
    QString imageUrl;
    /// What the library calls this folio: "front cover", "1r", "162v". The only
    /// name a scanned folio has, since it has no filename.
    QString imageLabel;
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
    /// Which leaves of the codex this is: "1r–4v".
    QString folios;
    QString date;
    /// What it is written on — "Paper", "Parchment".
    QString material;
    /// How it came to be where it is kept.
    QString provenance;
    /// What the Hebrew was rendered out of, where it is a rendering at all:
    /// "Translated from the Greek".
    ///
    /// Nothing fills this in but the transcriber. It is a judgement about the
    /// text rather than a fact a library records, which is why no catalogue
    /// Milah reads has ever carried it.
    QString translatedFrom;
    /// How firmly the answer above is held — one of TranslationCertainty, or
    /// empty where the transcriber has not said.
    ///
    /// A second question rather than a shade of the first: for several of these
    /// manuscripts whether the Hebrew renders a Greek text at all is the very
    /// thing under argument, and a catalogue that stated it flat would be
    /// taking a side.
    QString translatedFromCertainty;
    /// The older manuscript this one copies, where it is known to copy one:
    /// "Copied from Cambridge MS Oo.1.32". A judgement too.
    QString exemplar;
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

/// True when this verse number means the matter standing before verse 1 — an
/// incipit, a superscription, the scribe's heading to a chapter.
///
/// Written as verse 0, which is what a transcriber types and what a
/// versification carrying such matter calls it. "00" counts, and so does "0a":
/// a preamble a manuscript divides is still a preamble.
///
/// A preamble keeps the id Book.Chapter.0 inside Milah — that is what keys its
/// notes and what the export sorts by. It differs only in what is written out:
/// OSIS has an element for matter standing before the verses, and this is not
/// a verse. See serializeOsis.
bool isPreamble(const QString &verseNumber);

/// True while nothing has been read off this folio: no word typed, and no verse
/// numbered. What the workspace asks before it offers to say where to start.
///
/// Not the same as having no verses. A folio always carries one empty verse
/// holding one empty word, because there has to be somewhere to type — so
/// emptiness is a question about the contents, not about the count.
bool isUntouched(const TranscribedPage &page);

/// Text divided the way a transcriber would type it: whitespace separates
/// words, and a token that looksLikeVerseNumber() opens a verse.
///
/// For pasting. Words standing before any number become a verse with no number
/// of its own, which is what a fragment out of the middle of a chapter is.
QList<TranscribedVerse> parseTranscribedText(const QString &text);

/// How a verse should be named on screen: as much of its reference as is known.
///
/// A transcription is written before it is identified — the book may not have
/// been typed yet, and a verse being started has no number at all — so this
/// says the most it truthfully can rather than waiting for all three parts.
/// "Rev 4:1", then "4:1", then "Verse 1", and "Unnumbered" before that.
QString verseHeading(const TranscribedPage &page, int verseIndex);

/// The name a transcription of one book takes in the manuscript library.
///
/// `<OSIS id>_<Manuscript>_hebrew_commented.osis` — Rev_Sloane237_hebrew_
/// commented.osis. The same id every other part of Milah addresses the book
/// by, canonical or not. The manuscript's name is stripped to letters,
/// digits, dots and dashes, because it is a transcriber's free text and half
/// of what they might type is punctuation Windows refuses in a filename.
///
/// Never ends `_translation`: that suffix, and nothing in the file, is what
/// tells Milah a library text is a translation rather than a witness.
QString libraryFileName(const QString &bookOsisId, const QString &manuscriptName);

/// True while nothing has been read off any folio of this transcription.
///
/// Asked before leaving a folio writes the file. A transcriber walking a codex
/// to find where their text begins has made nothing to lose, and stopping them
/// for a filename made reading a manuscript cost more than transcribing it: the
/// Save As could be cancelled, and cancelling refused the page turn.
///
/// Folios only, deliberately. The manuscript details are the library's record
/// until the transcriber edits them, and editing them goes through
/// setMetadata(), which marks the transcription changed on its own account.
bool isUntouched(const TranscriptionDocument &document);

/// A piece of text made fit to stand inside a `.trscrpt` archive entry name.
///
/// The character that matters is the slash. A published scan's id is whatever
/// its catalogue says it is, and a catalogue that names no manuscript has only
/// the manifest address to give — so `images/https://gallica.bnf.fr/…-0001.jpg`
/// was being handed to the archive writer as an entry path. The doubled slash
/// in `https://` does not survive being cleaned, the entry stops equalling its
/// own clean form, ProjectStorage refuses the whole save, and a transcriber
/// whose codex happened to be catalogued that way was pinned to folio one for
/// ever — because turning the page needs a save that could never succeed.
///
/// Everything outside `[A-Za-z0-9._-]` becomes an underscore rather than being
/// dropped, so a name stays about as long and as readable as it was. That is
/// all this is for: the result is deliberately **not** unique — two ids
/// differing only in punctuation come out identical, as do two differing only
/// past the length cap. Nothing may use it to tell two folios apart. See
/// imageEntryFor(), where the folio's place in the document does that.
QString archiveNameFragment(const QString &text);

/// The entry path a folio's image takes inside the archive: `images/`, where the
/// folio sits in the document, and a readable remainder made out of `name`.
///
/// The position is what makes the entry unique, and it has to be. Two folios
/// called `1.jpg` out of different folders would otherwise be one entry and the
/// second would silently replace the first — and the same name also decides
/// which folio the image pane and the grid believe they are showing, so a
/// collision means turning the page without the picture changing.
QString imageEntryFor(int pageIndex, const QString &name);

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
