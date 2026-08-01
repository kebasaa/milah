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

/// Orders books by their canonical position, falling back to a plain string
/// comparison for anything outside the canon.
int compareBooks(const QString &left, const QString &right);

/// The book's name for a reader — "Revelation" for the OSIS id "Rev". An id
/// outside the canon comes back unchanged, so it still names its own book.
QString bookName(const QString &osisId);

/// Natural-order comparison, so that verse "10" sorts after verse "9". Kept
/// locale-independent on purpose: exported OSIS must not depend on the
/// machine's regional settings.
int compareNumericAware(const QString &left, const QString &right);

} // namespace milah
