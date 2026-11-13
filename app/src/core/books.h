#pragma once

#include <QString>
#include <QStringList>

namespace milah {

/// Every OSIS book id, in canonical order.
///
/// For places that offer the canon rather than read it — the transcription
/// toolbar, where the book is typed because a folio may not have been
/// identified yet, and the completer assists without refusing anything the
/// table has never heard of.
QStringList bookIds();

/// Every book's name for a reader, in canonical order. The counterpart of
/// bookIds(): what a transcriber is offered while typing, since one writes
/// "Revelation" and not "Rev".
QStringList bookNames();

/// The OSIS id for whatever the reader wrote — a name, an id, or either in the
/// wrong case or spacing, so "Revelation", "revelation" and "rev" all give Rev.
///
/// Empty when the canon has never heard of it, which is not an error: a
/// transcriber may be reading an apocryphal or non-canonical work, and is then
/// left to say what it should be called.
QString bookIdFor(const QString &nameOrId);

/// `raw` with only its letters and digits: what an osisID can carry as a book.
/// It separates book, chapter and verse with dots and a work prefix with a
/// colon, so "Tob.it" or "1 Enoch: Book" would make an id nobody could read
/// back. Stripped rather than refused: the transcriber meant a book, not a
/// syntax.
QString sanitisedBookId(const QString &raw);

/// The id a book is stored and exported under. The canonical OSIS id when the
/// canon knows the book, however it arrived — "JAS" from an old file or a scan
/// catalogue, "james", "Jas" — so one book is never filed under two ids.
/// Otherwise sanitisedBookId().
QString normalisedBookId(const QString &raw);

/// Which part of the canon a book belongs to, for grouping a library: the Hebrew
/// Bible with the deuterocanon, the New Testament, or neither — a
/// non-biblical work, or an id the canon does not know. Accepts a name or an id
/// in any case ("JAS", "james"), and the collection codes "OT" and "NT" that
/// whole-testament files are named with.
enum class Testament { Old, New, Other };
Testament testamentOf(const QString &book);

/// "Old Testament", "New Testament", "Other texts".
QString testamentName(Testament testament);

/// A catalogue's own `category` ("ot", "nt", "other", any case) where it gives
/// one, else testamentOf(`book`) — which is right for every manifest written
/// before categories existed.
Testament testamentFromCategory(const QString &category, const QString &book);

/// Orders the books of the Hebrew Bible as the Tanakh does — Torah, Nevi'im,
/// Ketuvim, ending with Chronicles — for browsing Hebrew manuscripts, which
/// follow it. Anything else falls back to compareBooks().
int compareTanakh(const QString &left, const QString &right);

/// Orders books by their canonical position, falling back to a plain string
/// comparison for anything outside the canon.
int compareBooks(const QString &left, const QString &right);

/// The book's name for a reader — "Revelation" for the OSIS id "Rev". An id
/// outside the canon comes back unchanged, so it still names its own book.
QString bookName(const QString &osisId);

/// "Jas.1.25" as a reader writes it: "Jas 1:25".
///
/// Anything not of that shape is handed back untouched rather than mangled — a
/// menu entry or a button naming a place is worth less if the place is
/// unrecognisable, and worth nothing if it is wrong.
///
/// Here rather than beside one of the two widgets that says it, because both of
/// them name the same place for the same reason: continuing a transcription
/// without saying where from asks the transcriber to trust it blindly.
QString readableVerseId(const QString &id);

/// Natural-order comparison, so that verse "10" sorts after verse "9". Kept
/// locale-independent on purpose: exported OSIS must not depend on the
/// machine's regional settings.
int compareNumericAware(const QString &left, const QString &right);

} // namespace milah
