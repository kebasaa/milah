#pragma once

#include "core/osis_fill.h"
#include "core/types.h"

#include <QDialog>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace milah {

struct TranscribedPage;

/// One line of the folio as the recogniser found it. Outside the dialog because
/// moc will not parse a nested class inside a Q_OBJECT body.
struct FolioLine
{
    /// The recogniser's own index for it, which is what a word carries.
    int index = 0;
    /// Its word boxes, in reading order.
    QList<QRect> boxes;
    /// How many words a fill lays here before anybody nudges it: the box count,
    /// or what the transcriber has already said this line of the leaf holds.
    /// See LineFill's wordCounts().
    int words = 0;
    /// What the recogniser read there — shown for the lines the fill leaves
    /// alone, so the start line can be found against the picture.
    QString read;
};

/// Putting a transcription that already exists onto the folio it was made from.
///
/// Several of these manuscripts have been transcribed and published — Matthew
/// and James of MS Oo.1.32 among them — while Kraken reads that hand into noise.
/// Typing 250 words of a cursive to make training data out of a folio whose text
/// is already written down is work nobody should do twice.
///
/// **The lines are the fixed thing and the words are laid into them.** A
/// recogniser draws a box round each word it thinks it found, and on a hand it
/// was not trained for it gets the count wrong constantly — so pairing one word
/// with one box, as this once did, threw away every word the recogniser had not
/// found. Instead each line is given a number of words and its boxes are cut up
/// or joined to fit; see core/line_fill.h.
///
/// **The OSIS says nothing about folios.** Its elements are verse, chapter, note
/// and div; no page mark of any kind. So where on the leaf the text begins is the
/// one thing the transcriber has to supply. Milah tried to work it out — sliding
/// the recognised words along the book and scoring them — and got one folio of
/// three right, with the correct one's margin no better than the wrong ones'. At
/// 41% noise there is not enough in a reading to place it. Measured, then
/// dropped rather than shipped as a guess.
class OsisFillDialog final : public QDialog
{
    Q_OBJECT

public:
    /// Everything this needs to do its work, and the two things it cannot do
    /// itself.
    struct Setup
    {
        /// The folio, read again after a recognition rather than copied once —
        /// the window opens before the folio has been read and the lines have to
        /// appear when they arrive.
        std::function<const TranscribedPage *()> folio;
        /// Runs the recognition, over this window. True when the folio came back
        /// with something on it.
        std::function<bool()> readFolio;
        /// Where the transcriber pointed, in the folio image's own pixels. Null
        /// for the top of the page.
        ///
        /// A place rather than a line, because on a folio nothing has read there
        /// are no lines to name — and that is the folio this is most often used
        /// on, since the window reads it. Resolved in takeLines(), which runs
        /// again once the recognition has produced some.
        QPoint folioPixel;
    };

    OsisFillDialog(Setup setup, QWidget *parent = nullptr);

    /// Bound after construction, because the recognition has to raise its
    /// progress over this window and so needs it to exist first.
    void setReader(std::function<bool()> readFolio) { m_setup.readFolio = std::move(readFolio); }

    /// The lines the fill covers, from the start line down. Lines above it, and
    /// lines past the end of the passage, are absent and must be left alone.
    QList<FilledLine> filled() const { return m_filled; }
    /// Which verse each filled word came from, one per word of `filled()` read
    /// in order — so the folio can be cut into verses the way the source is.
    QStringList wordVerses() const { return m_filledVerses; }

    /// The transcription that was used, so the next folio can carry on from it
    /// without being asked for it again.
    QString sourcePath() const { return m_sourcePath; }

    /// Where this folio's text begins, for a re-flow to lay it again from. The
    /// verse as an OSIS id, and how many of its words the leaf before this one
    /// already held.
    QString startVerse() const;
    int startWord() const;

    /// Where this folio stopped, for the next one to resume at.
    QString endVerse() const { return m_endVerse; }
    int endWord() const { return m_endWord; }
    /// What was taken, for the message afterwards.
    QString range() const { return m_range; }

private:
    /// Raises the file chooser. Starting a new transcription means choosing its
    /// file, so this is what that entry does.
    void chooseSource();
    /// Reads and takes up `path`. Returns why not, or an empty string — the
    /// remembered file may have been moved since, and a window that opened
    /// silently empty would look like the remembering had failed.
    ///
    QString loadSource(const QString &path);
    /// Reads the folio, then takes its lines. The window stays open: the start
    /// line and the breaks are the point of it, and neither can be decided
    /// before the lines exist.
    void readFolio();
    /// The folio's lines as the recogniser left them, or empty before it has run.
    void takeLines();
    void rebuildChapters();
    /// Reads the passage out of the source, from the chosen verse on, through
    /// gatherPassage() in core/osis_fill.h.
    void gather();
    /// Lays the passage into the lines and redraws. Everything the buttons do is
    /// set a number and call this, which is what makes them all reversible.
    void rebuild();
    void setStartLine(int line);
    /// This line takes one more word, or one fewer; every line below re-flows.
    void nudge(int by);

    Setup m_setup;
    QList<FolioLine> m_lines;
    int m_startLine = 0;
    /// Words per line, from the start line down. Sized to m_lines for simplicity;
    /// entries above the start line are unused.
    QList<int> m_counts;
    SourceDocument m_source;
    QString m_sourcePath;
    /// Every word of the chosen book from the chosen verse on, and the verse
    /// each came from.
    QStringList m_passage;
    QStringList m_passageVerses;

    QList<FilledLine> m_filled;
    QStringList m_filledVerses;
    QString m_endVerse;
    int m_endWord = -1;
    QString m_range;

    QLabel *m_sourceName = nullptr;
    QComboBox *m_book = nullptr;
    QComboBox *m_chapter = nullptr;
    QSpinBox *m_verse = nullptr;
    /// How many words of that verse the folio before this one already took.
    /// Visible, because it is the difference between the passage starting where
    /// the controls say and starting somewhere else.
    QSpinBox *m_skip = nullptr;
    QLabel *m_summary = nullptr;
    QListWidget *m_lineList = nullptr;
    QPushButton *m_fill = nullptr;
    /// Live only once the folio has lines: neither means anything before that.
    QPushButton *m_startsHere = nullptr;
    QPushButton *m_startsAtTop = nullptr;
    QPushButton *m_fewer = nullptr;
    QPushButton *m_more = nullptr;
};

} // namespace milah
