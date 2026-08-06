#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace milah {

class TranscriptionController;

/// What the folio is, as against what it says: whose manuscript, whose hand,
/// where it is kept and under what mark.
///
/// Written once per transcription rather than per folio, because it describes
/// the codex and not the page. Deliberately not validated and mostly not
/// required — a transcriber describing a manuscript is recording what they
/// know, and a form that refuses an unknown shelfmark records less than one
/// that accepts a blank.
class TranscriptionMetaWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TranscriptionMetaWidget(
        TranscriptionController *controller,
        QWidget *parent = nullptr);

    /// Redraws from the controller. Called on documentChanged, and once at the
    /// end of construction.
    void refresh();

private:
    void save();
    /// Save lights only when a field actually differs from what was last read
    /// or written, so a panel that has merely been looked at does not offer to
    /// write over itself.
    void updateSaveState();
    /// Everything the fields currently say, as a single string, for comparing
    /// against what was loaded.
    QString fingerprint() const;

    TranscriptionController *m_controller = nullptr;

    QLineEdit *m_manuscriptName = nullptr;
    QLineEdit *m_transcriber = nullptr;
    QLineEdit *m_origin = nullptr;
    QLineEdit *m_libraryMark = nullptr;
    QLineEdit *m_shelfmark = nullptr;
    QLineEdit *m_folios = nullptr;
    QLineEdit *m_date = nullptr;
    QLineEdit *m_material = nullptr;
    QLineEdit *m_provenance = nullptr;
    QLineEdit *m_translatedFrom = nullptr;
    QComboBox *m_translatedFromCertainty = nullptr;
    QLineEdit *m_exemplar = nullptr;
    QLineEdit *m_language = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QPushButton *m_save = nullptr;

    /// What the fields said when they were last filled from the document.
    QString m_saved;
};

} // namespace milah
