#pragma once

#include <QString>
#include <QWidget>

class QScrollArea;
class QSplitter;

namespace milah {

class ManuscriptImageView;
class TranscriptionController;
class TranscriptionGridWidget;

/// The transcription workspace: the folio above, and what is being read off it
/// below.
///
/// A splitter rather than a fixed division, because how much of the screen the
/// photograph is worth depends on the hand — a clear square Ashkenazi script
/// needs a glance where a cramped cursive needs half the window.
class TranscriptionWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TranscriptionWidget(
        TranscriptionController *controller,
        QWidget *parent = nullptr);

public slots:
    void setMagnifierEnabled(bool enabled);
    void setOverlayVisible(bool visible);
    void setLineBoxesVisible(bool visible);

signals:
    /// Asks for the readings to be shown. Sent rather than done, because the
    /// eye on the toolbar is what holds that state and a view that turned it on
    /// behind the button's back would leave the two disagreeing.
    void overlayWanted();

private:
    void showCurrentPage();
    /// Hands the folio's words to the image pane, so the boxes follow the text
    /// as it is corrected. Cheap when there are none, which is every folio
    /// nobody has run a recogniser over.
    void refreshOverlay();

    TranscriptionController *m_controller = nullptr;
    QSplitter *m_splitter = nullptr;
    ManuscriptImageView *m_image = nullptr;
    QScrollArea *m_imageArea = nullptr;
    TranscriptionGridWidget *m_grid = nullptr;
    QScrollArea *m_textArea = nullptr;
    /// Which folio is currently decoded, so a document change that is not a
    /// change of folio does not redecode it.
    QString m_shownEntry;
};

} // namespace milah
