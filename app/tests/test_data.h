#pragma once

#include <QString>
#include <QStringList>

/**
 * Sample OSIS documents used by the tests.
 *
 * They live here rather than beside the tests because moc mis-parses raw
 * string literals: a test file containing one is silently reported as having
 * "no relevant classes", and the QTest class then fails to link. Keeping the
 * samples in a translation unit moc never touches lets them stay readable.
 */
namespace milah_test {

/// A minimal witness with one verse and one inline note.
extern const char *const kSampleOsis;

/// The XML declaration of kSampleOsis, and a DOCTYPE to swap it for.
extern const char *const kXmlDeclaration;
extern const char *const kDoctypeDeclaration;

/**
 * Shaped like the converter's Sloane 273 output: milestoned verses, an incipit
 * title outside any verse, a chapter title, folio boundaries, and notes typed
 * `explanation` rather than the non-standard `footnote`.
 */
extern const char *const kApparatusManuscript;

/// A single-verse witness carrying `text`, for consensus tests. `text` is
/// substituted into the verse element as it stands, so it may carry inline OSIS
/// markup — a `<note>` among the words, for instance.
QString witnessOsis(const QString &id, const QString &text);

/// A witness holding one verse in each of `chapters`, each named "Book.Chapter"
/// — for coverage tests, where only which places a manuscript reaches matters
/// and not what it says there.
QString witnessOsisCovering(const QString &id, const QStringList &chapters);

} // namespace milah_test
