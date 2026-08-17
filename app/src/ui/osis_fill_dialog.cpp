#include "ui/osis_fill_dialog.h"

#include "core/line_fill.h"
#include "core/osis.h"
#include "core/transcription.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace milah {
namespace {

/// The folio's lines as the recogniser left them: its boxes, and what it read.
QList<FolioLine> linesOf(const TranscribedPage &page)
{
    QMap<int, FolioLine> found;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line < 0 || word.box.isNull()) {
                continue;
            }
            FolioLine &line = found[word.line];
            line.index = word.line;
            line.boxes.append(word.box);
            if (!word.hebrew.isEmpty()) {
                line.read += line.read.isEmpty() ? word.hebrew
                                                 : QLatin1Char(' ') + word.hebrew;
            }
        }
    }
    return found.values();
}

/// Every word of the folio, in document order — what lineAtPoint() asks for.
QList<TranscribedWord> wordsOf(const TranscribedPage &page)
{
    QList<TranscribedWord> words;
    for (const TranscribedVerse &verse : page.verses) {
        words += verse.words;
    }
    return words;
}

} // namespace

OsisFillDialog::OsisFillDialog(Setup setup, QWidget *parent)
    : QDialog(parent)
    , m_setup(std::move(setup))
{
    setWindowTitle(QStringLiteral("Fill this folio from a transcription"));
    resize(780, 660);

    auto *heading = new QLabel(QStringLiteral(
        "The words are laid into the folio's lines, and each line's boxes are cut "
        "up or joined so that <b>every word gets one</b> — including the ones the "
        "recogniser never found. Only the line breaks have to be right: a model "
        "is trained on whole lines, so which box inside a line a word lands on "
        "does not matter.<p>Nothing is marked checked. A machine put these words "
        "here, and that is what unchecked means.</p>"));
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);

    m_sourceName = new QLabel(QStringLiteral("No transcription chosen."));
    m_sourceName->setWordWrap(true);
    auto *choose = new QPushButton(QStringLiteral("Choose a transcription…"));
    connect(choose, &QPushButton::clicked, this, &OsisFillDialog::chooseSource);

    m_book = new QComboBox;
    m_chapter = new QComboBox;
    m_verse = new QSpinBox;
    m_verse->setRange(1, 200);
    m_verse->setToolTip(QStringLiteral(
        "The first verse of the text on this folio. The OSIS carries no page "
        "marks — its elements are verse, chapter, note and div — so this is the "
        "one thing it cannot tell Milah."));
    m_skip = new QSpinBox;
    m_skip->setRange(0, 999);
    m_skip->setToolTip(QStringLiteral(
        "Where the folio before this one stopped mid-verse, this is how many of "
        "that verse's words it already took. Zero means the passage starts at "
        "its own first word — which is what pointing at a line on the folio "
        "means."));
    connect(m_book, &QComboBox::currentIndexChanged, this, &OsisFillDialog::rebuildChapters);
    connect(m_chapter, &QComboBox::currentIndexChanged, this, [this] {
        gather();
        rebuild();
    });
    connect(m_verse, &QSpinBox::valueChanged, this, [this] {
        gather();
        rebuild();
    });
    connect(m_skip, &QSpinBox::valueChanged, this, [this] {
        gather();
        rebuild();
    });

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Book"), m_book);
    form->addRow(QStringLiteral("Chapter"), m_chapter);
    form->addRow(QStringLiteral("First verse of the text"), m_verse);
    form->addRow(QStringLiteral("Skipping the first … words of it"), m_skip);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);

    m_lineList = new QListWidget;
    m_lineList->setToolTip(QStringLiteral(
        "Each line as it would be filled. Lines the fill does not reach still "
        "show what the recogniser read, so you can find your place against the "
        "picture."));

    m_startsHere = new QPushButton(QStringLiteral("The text starts on this line"));
    m_startsAtTop = new QPushButton(QStringLiteral("Starts at the top"));
    m_fewer = new QPushButton(QStringLiteral("one word fewer"));
    m_more = new QPushButton(QStringLiteral("one word more"));
    m_fewer->setToolTip(QStringLiteral(
        "The selected line takes one word fewer; every line below re-flows."));
    m_more->setToolTip(QStringLiteral(
        "The selected line takes one word more; every line below re-flows."));
    connect(m_startsHere, &QPushButton::clicked, this, [this] {
        setStartLine(std::max(0, m_lineList->currentRow()));
    });
    connect(m_startsAtTop, &QPushButton::clicked, this, [this] { setStartLine(0); });
    connect(m_fewer, &QPushButton::clicked, this, [this] { nudge(-1); });
    connect(m_more, &QPushButton::clicked, this, [this] { nudge(1); });

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_startsHere);
    controls->addWidget(m_startsAtTop);
    controls->addStretch();
    controls->addWidget(m_fewer);
    controls->addWidget(m_more);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    // Added with no role, so pressing it does not close the window: on a folio
    // nothing has read yet it means "read it", and the reading is the middle of
    // the job rather than the end of it.
    m_fill = buttons->addButton(QStringLiteral("Fill the folio"), QDialogButtonBox::ActionRole);
    m_fill->setEnabled(false);
    connect(m_fill, &QPushButton::clicked, this, [this] {
        if (m_lines.isEmpty()) {
            readFolio();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(heading);
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_sourceName, 1);
    sourceRow->addWidget(choose);
    outer->addLayout(sourceRow);
    outer->addLayout(form);
    outer->addWidget(m_summary);
    outer->addWidget(m_lineList, 1);
    outer->addLayout(controls);
    outer->addWidget(buttons);

    takeLines();
    rebuild();

    // Starting a new transcription means choosing its file, so that is what
    // happens — after this window is up, so cancelling the chooser leaves you
    // here rather than nowhere.
    QTimer::singleShot(0, this, &OsisFillDialog::chooseSource);
}

void OsisFillDialog::takeLines()
{
    const TranscribedPage *page = m_setup.folio ? m_setup.folio() : nullptr;
    m_lines = page ? linesOf(*page) : QList<FolioLine>();
    m_counts.clear();
    for (const FolioLine &line : m_lines) {
        m_counts.append(int(line.boxes.size()));
    }

    // Where the transcriber pointed, turned into a line here rather than at the
    // moment of the click — the folio commonly had no lines then, and this runs
    // again once the recognition has made some.
    m_startLine = 0;
    if (page && !m_setup.folioPixel.isNull()) {
        const int line = lineAtPoint(wordsOf(*page), m_setup.folioPixel);
        for (int index = 0; index < m_lines.size(); ++index) {
            if (m_lines.at(index).index == line) {
                m_startLine = index;
                break;
            }
        }
    }
}

void OsisFillDialog::readFolio()
{
    if (!m_setup.readFolio) {
        return;
    }
    // Over this window, not behind it — everything the recognition raises has to
    // be reachable while this dialog holds the application's input.
    m_setup.readFolio();
    takeLines();
    rebuild();
    if (m_lines.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("Milah"),
            QStringLiteral("Nothing was read off this folio, so there are no lines to "
                           "lay the transcription into.<p>The transcription you chose "
                           "is still chosen — try again once the recogniser can read "
                           "the folio.</p>"));
    }
}

void OsisFillDialog::chooseSource()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose a transcription of this manuscript"),
        QSettings().value(QStringLiteral("paths/lastOsis")).toString(),
        QStringLiteral("OSIS files (*.osis *.xml);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    // Its own setting: this is a published transcription, and remembering it
    // beside the folder somebody last saved a folio into would send them back
    // and forth between two places every folio.
    QSettings().setValue(QStringLiteral("paths/lastOsis"), path);

    const QString failure = loadSource(path);
    if (!failure.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Milah"), failure);
    }
}

QString OsisFillDialog::loadSource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("%1 could not be opened.\n%2")
            .arg(QFileInfo(path).fileName(), file.errorString());
    }

    SourceDocument parsed;
    try {
        // The same reader the comparison side loads these very files with, so a
        // file that opens there opens here.
        parsed = parseOsis(QString::fromUtf8(file.readAll()), ParseOptions{});
    } catch (const OsisError &failure) {
        return failure.message();
    }

    QStringList books;
    for (const SourceVerse &verse : parsed.verses) {
        if (!books.contains(verse.reference.book)) {
            books.append(verse.reference.book);
        }
    }

    // Checked before anything is taken up, so a file that cannot answer leaves
    // the window exactly as it was rather than half-adopted — the source name
    // changed, the books replaced, and no place in them to go to.
    m_source = parsed;
    m_sourcePath = path;
    m_sourceName->setText(QFileInfo(path).fileName());
    m_book->clear();
    m_book->addItems(books);

    // The beginning of what was chosen — first book, first chapter, verse one.
    // This window is only ever reached to *start* a transcription; carrying one
    // on is TranscriptionController::continueFill(), which needs no window
    // because it has nothing to ask.
    rebuildChapters();
    return QString();
}

QString OsisFillDialog::startVerse() const
{
    if (m_book->currentText().isEmpty() || m_chapter->count() == 0) {
        return QString();
    }
    return QStringLiteral("%1.%2.%3")
        .arg(m_book->currentText())
        .arg(m_chapter->currentData().toInt())
        .arg(m_verse->value());
}

int OsisFillDialog::startWord() const
{
    return m_skip->value();
}

void OsisFillDialog::rebuildChapters()
{
    const QString book = m_book->currentText();
    QList<int> chapters;
    for (const SourceVerse &verse : m_source.verses) {
        if (verse.reference.book == book && !chapters.contains(verse.reference.chapter)) {
            chapters.append(verse.reference.chapter);
        }
    }
    std::sort(chapters.begin(), chapters.end());

    const QSignalBlocker quiet(m_chapter);
    const QString wanted = m_chapter->currentText();
    m_chapter->clear();
    for (const int chapter : chapters) {
        m_chapter->addItem(QString::number(chapter), chapter);
    }
    if (!wanted.isEmpty() && m_chapter->findText(wanted) >= 0) {
        m_chapter->setCurrentText(wanted);
    }
    gather();
    rebuild();
}

void OsisFillDialog::gather()
{
    m_passage.clear();
    m_passageVerses.clear();
    if (m_source.verses.isEmpty() || m_chapter->count() == 0) {
        return;
    }

    // The same reading of "the passage starting here" that a continuation uses.
    // Two definitions of it is one more than the number that can be right, and
    // the difference shows up as a folio repeating words the leaf before it had
    // rather than as anything that looks like a fault.
    const Passage passage = gatherPassage(
        m_source,
        m_book->currentText(),
        m_chapter->currentData().toInt(),
        m_verse->value(),
        m_skip->value());
    m_passage = passage.words;
    m_passageVerses = passage.verses;
}

void OsisFillDialog::setStartLine(int line)
{
    if (m_lines.isEmpty()) {
        return;
    }
    m_startLine = std::clamp(line, 0, int(m_lines.size()) - 1);
    // The counts below it start again from what the recogniser found, because a
    // nudge made against a different starting point means nothing.
    for (int index = 0; index < m_counts.size(); ++index) {
        m_counts[index] = int(m_lines.at(index).boxes.size());
    }
    rebuild();
}

void OsisFillDialog::nudge(int by)
{
    const int row = m_lineList->currentRow();
    if (row < m_startLine || row >= m_counts.size()) {
        return;
    }
    m_counts[row] = std::max(0, m_counts.at(row) + by);
    rebuild();
}

void OsisFillDialog::rebuild()
{
    m_filled.clear();
    m_filledVerses.clear();
    m_endVerse.clear();
    m_endWord = -1;
    m_range.clear();

    int taken = 0;
    for (const LineFill::Laid &laid :
         LineFill::layOut(m_counts, m_startLine, 0, int(m_passage.size()))) {
        FilledLine line;
        line.index = m_lines.at(laid.line).index;
        line.words = m_passage.mid(laid.from, laid.count);
        line.boxes = LineFill::place(m_lines.at(laid.line).boxes, laid.count);
        if (line.boxes.size() != line.words.size()) {
            // A line with no boxes at all, which linesOf() cannot produce — but
            // a box each is what the caller is promised, so it is checked
            // rather than assumed.
            continue;
        }
        m_filledVerses += m_passageVerses.mid(laid.from, laid.count);
        m_filled.append(line);
        taken = laid.from + laid.count;
    }

    if (!m_filled.isEmpty()) {
        m_range = QStringLiteral("%1 – %2")
                      .arg(m_passageVerses.first(), m_passageVerses.at(taken - 1));
        m_endVerse = m_passageVerses.at(taken - 1);
        // How many words of that verse this folio took, so the next leaf can
        // carry on inside it.
        m_endWord = 0;
        for (int index = taken - 1; index >= 0 && m_passageVerses.at(index) == m_endVerse;
             --index) {
            ++m_endWord;
        }
    }

    // Drawn last, so it shows what was decided rather than what was asked for.
    m_lineList->clear();
    int at = 0;
    for (int index = 0; index < m_lines.size(); ++index) {
        QString text;
        if (index < m_startLine) {
            text = QStringLiteral("‹ %1 ›").arg(m_lines.at(index).read);
        } else if (at < m_filled.size() && m_filled.at(at).index == m_lines.at(index).index) {
            text = m_filled.at(at).words.join(QLatin1Char(' '));
            ++at;
        } else {
            text = QStringLiteral("‹ %1 ›").arg(m_lines.at(index).read);
        }
        m_lineList->addItem(
            QStringLiteral("%1 %2 %3")
                .arg(index == m_startLine ? QStringLiteral("▶") : QStringLiteral(" "))
                .arg(m_lines.at(index).index + 1, 3)
                .arg(text));
    }
    if (m_lineList->currentRow() < 0 && m_lineList->count() > 0) {
        m_lineList->setCurrentRow(m_startLine);
    }

    // The line-level controls mean nothing before there are lines.
    const bool haveLines = !m_lines.isEmpty();
    m_startsHere->setEnabled(haveLines);
    m_startsAtTop->setEnabled(haveLines);
    m_fewer->setEnabled(haveLines);
    m_more->setEnabled(haveLines);

    if (!haveLines) {
        m_fill->setText(QStringLiteral("Read the folio and fill…"));
        m_fill->setEnabled(!m_sourcePath.isEmpty());
        // The folio has not been read. Choosing the transcription is the part
        // that needs a person, so it happens first and the minute of reading is
        // spent afterwards rather than before.
        m_lineList->addItem(
            QStringLiteral("This folio has not been read yet. Choose a transcription "
                           "above, then press Read the folio and fill…"));
        m_summary->setText(
            m_sourcePath.isEmpty()
                ? QStringLiteral("Choose the transcription this folio was made from.")
                : QStringLiteral("Reading takes a minute or two on this machine."));
        return;
    }

    m_fill->setText(QStringLiteral("Fill the folio"));
    m_fill->setEnabled(!m_filled.isEmpty());
    if (m_sourcePath.isEmpty()) {
        m_summary->setText(QStringLiteral("%1 line(s) on this folio. Choose a "
                                          "transcription to fill from.")
                               .arg(m_lines.size()));
        return;
    }
    m_summary->setText(
        QStringLiteral("%1 word(s) over %2 line(s), starting on line %3%4.")
            .arg(taken)
            .arg(m_filled.size())
            .arg(m_lines.at(m_startLine).index + 1)
            .arg(m_range.isEmpty() ? QString() : QStringLiteral(" — %1").arg(m_range)));
}

} // namespace milah
