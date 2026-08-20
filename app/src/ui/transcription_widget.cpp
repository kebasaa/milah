#include "ui/transcription_widget.h"

#include "transcription_controller.h"
#include "ui/manuscript_image_view.h"
#include "ui/transcription_grid_widget.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

namespace milah {
namespace {

/// "Jas.1.25" as a reader writes it: "Jas 1:25". Anything not of that shape is
/// handed back untouched rather than mangled — a menu entry naming a place is
/// worth less if the place is unrecognisable.
QString readableVerseId(const QString &id)
{
    const QStringList parts = id.split(QLatin1Char('.'));
    if (parts.size() != 3) {
        return id;
    }
    return QStringLiteral("%1 %2:%3").arg(parts.at(0), parts.at(1), parts.at(2));
}

} // namespace

TranscriptionWidget::TranscriptionWidget(
    TranscriptionController *controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("transcriptionWorkspace"));

    m_image = new ManuscriptImageView;
    m_imageArea = new QScrollArea;
    m_imageArea->setWidget(m_image);
    m_imageArea->setWidgetResizable(true);
    m_imageArea->setFrameShape(QFrame::NoFrame);
    m_imageArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    // The folio is drawn to the width it is given, so a scrollbar appearing
    // would change that width and with it the height that made it appear.
    // Reserving it is the same bargain the verse list makes.
    m_imageArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    connect(m_image, &ManuscriptImageView::fillRequested, this, [this](QPoint folioPixel, bool carryOn) {
        // Shown, because checking where the pour landed against the ink is the
        // whole of the job afterwards and the boxes are the only thing on screen
        // that says where the lines fall. Only when there are any: an eye
        // toggled onto a folio with nothing to draw shows nothing.
        if (m_image->hasWordBoxes() && !m_image->overlayVisible()) {
            emit overlayWanted();
        }
        m_controller->fillFromOsis(folioPixel, carryOn);
    });

    connect(
        m_image,
        &ManuscriptImageView::marginalToggled,
        this,
        [this](QRect box, bool marginal) { m_controller->setMarginalAt(box, marginal); });

    connect(
        m_image,
        &ManuscriptImageView::wordEdited,
        this,
        [this](QRect box, QString hebrew) { m_controller->setWordAt(box, hebrew); });

    connect(
        m_image,
        &ManuscriptImageView::lineBreakToggled,
        this,
        [this](QRect box, bool endsLine) { m_controller->setLineBreakAt(box, endsLine); });

    connect(
        m_image,
        &ManuscriptImageView::lineJoinRequested,
        this,
        [this](int line) { m_controller->joinLineAt(line); });

    connect(
        m_image,
        &ManuscriptImageView::lineMarginalToggled,
        this,
        [this](int line, bool marginal) { m_controller->setLineMarginal(line, marginal); });

    connect(
        m_image,
        &ManuscriptImageView::lineBrokenBefore,
        this,
        [this](QRect box) { m_controller->breakLineBefore(box); });

    connect(
        m_image,
        &ManuscriptImageView::wordPulledUp,
        this,
        [this](QRect box) { m_controller->pullWordUp(box); });

    m_grid = new TranscriptionGridWidget(m_controller);
    m_textArea = new QScrollArea;
    m_textArea->setWidget(m_grid);
    m_textArea->setWidgetResizable(true);
    m_textArea->setFrameShape(QFrame::NoFrame);
    // The bands are packed to fit this viewport, for the same reason.
    m_textArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    m_splitter = new QSplitter(Qt::Vertical);
    m_splitter->setObjectName(QStringLiteral("transcriptionSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_imageArea);
    m_splitter->addWidget(m_textArea);
    // The folio opens with rather more of the window than the text, because at
    // the start there is no text; the transcriber moves the divider once and it
    // stays where they put it for the rest of the session.
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(m_splitter);

    connect(
        m_controller,
        &TranscriptionController::pageChanged,
        this,
        &TranscriptionWidget::showCurrentPage);
    connect(
        m_controller,
        &TranscriptionController::documentChanged,
        this,
        &TranscriptionWidget::showCurrentPage);
    // The boxes belong to the words, so a correction moves the reading drawn on
    // the folio with it. wordChecked is separate from versesChanged on purpose —
    // see the signal — and both mean the overlay is out of date.
    connect(
        m_controller,
        &TranscriptionController::versesChanged,
        this,
        &TranscriptionWidget::refreshOverlay);
    connect(
        m_controller,
        &TranscriptionController::wordChecked,
        this,
        [this] { refreshOverlay(); });

    showCurrentPage();
}

void TranscriptionWidget::setMagnifierEnabled(bool enabled)
{
    m_image->setMagnifierEnabled(enabled);
}

void TranscriptionWidget::setOverlayVisible(bool visible)
{
    m_image->setOverlayVisible(visible);
}

void TranscriptionWidget::setLineBoxesVisible(bool visible)
{
    m_image->setLineBoxesVisible(visible);
}

void TranscriptionWidget::refreshOverlay()
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        m_image->setWords({});
        m_image->setLines({});
        m_image->setContinuation(QString());
        return;
    }
    QList<TranscribedWord> words;
    for (const TranscribedVerse &verse : page->verses) {
        words.append(verse.words);
    }
    m_image->setWords(words);
    m_image->setLines(page->lines);

    // Here because this already runs on every change of folio and every change
    // of text, and where the folio before this one stopped changes with both.
    const ResumePoint resume = m_controller->resumePoint();
    m_image->setContinuation(
        resume.isValid() ? readableVerseId(resume.verse) : QString());
}

void TranscriptionWidget::showCurrentPage()
{
    const TranscribedPage *page = m_controller->currentPage();
    if (!page) {
        m_shownEntry.clear();
        m_image->clear();
        return;
    }
    // Saving the file and editing the manuscript details both report a document
    // change, and neither is a different folio. Decoding the scan again for
    // them would cost a scan's worth of work and — worse — throw the reader
    // back to the top of a page they were halfway down.
    if (page->imageEntry == m_shownEntry) {
        // The picture is the same; what is drawn on it need not be.
        refreshOverlay();
        return;
    }
    const QByteArray bytes = m_controller->currentImageBytes();
    m_image->setImageData(bytes, page->imageName, m_controller->imageFailure());
    // After the image and never before it: showing a folio drops the overlay,
    // because the boxes belonged to the folio being replaced.
    refreshOverlay();
    // Remembered only once something was actually shown. A folio that failed to
    // arrive has nothing to decode and nothing to scroll, so the guard above
    // buys nothing for it — and recording it as shown would keep the notice on
    // screen after a later attempt succeeded.
    m_shownEntry = bytes.isEmpty() ? QString() : page->imageEntry;
    // A new folio starts at the top of itself, not wherever the last one was
    // being read.
    m_imageArea->verticalScrollBar()->setValue(0);
}

} // namespace milah
