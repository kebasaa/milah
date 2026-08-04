#include "ui/transcription_widget.h"

#include "transcription_controller.h"
#include "ui/manuscript_image_view.h"
#include "ui/transcription_grid_widget.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

namespace milah {

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

    showCurrentPage();
}

void TranscriptionWidget::setMagnifierEnabled(bool enabled)
{
    m_image->setMagnifierEnabled(enabled);
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
        return;
    }
    const QByteArray bytes = m_controller->currentImageBytes();
    m_image->setImageData(bytes, page->imageName, m_controller->imageFailure());
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
