#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

class QByteArray;

namespace milah {

/// What a handwriting recogniser made of a folio: words, and where on the
/// picture each one was read from.
///
/// Two formats say this, and Milah reads both. ALTO is what kraken writes, and
/// so what Milah's own Transcribe button produces. PAGE is what an institution
/// running eScriptorium is likely to hand over. They differ in spelling rather
/// than in substance, so one reader answers both and everything downstream —
/// the overlay, the grid, the unchecked flag — never learns which was opened.

/// One word, and the ink it came from.
struct RecognisedWord
{
    QString text;
    /// In the pixel space of the image the recogniser was given — see
    /// RecognisedPage::imageSize, which is what makes it comparable to ours.
    QRect box;
    /// Which line it came from, counting from zero. For ordering and for
    /// telling a line break from a word gap; never shown.
    int line = 0;
};

struct RecognisedPage
{
    /// The page size the file declares, not assumed equal to the folio Milah
    /// holds: a box scaled by the wrong factor lands somewhere plausible and
    /// wrong, which is the worst way for this to fail.
    QSize imageSize;
    QList<RecognisedWord> words;
};

/// Reads an ALTO or PAGE document, told apart by its root element.
///
/// Returns an empty page and fills `error` on anything it cannot honestly read.
/// Refuses rather than guesses in three places, each of which would otherwise
/// draw boxes in the wrong place and look convincing doing it: a measurement
/// unit that is not pixels, a page that declares no size, and a PAGE file
/// segmented into lines but not into words.
///
/// `error` may be null. Nothing here throws: unlike an OSIS file, this is the
/// output of a program the transcriber just ran, and a message beside the
/// Transcribe button is the whole of what they can do about it.
RecognisedPage parseRecognisedPage(const QByteArray &xml, QString *error);

} // namespace milah
