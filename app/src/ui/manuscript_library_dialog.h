#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QListWidget;
class QPushButton;

namespace milah {

/// Offers the manuscripts already in the local library, so loading one does not
/// mean remembering where on disk it lives.
///
/// Browsing for a file is still offered: the corpus in data/01_osis and
/// anything the pdf2osis pipeline produces never passes through the library,
/// and losing the ability to open those would be a poor trade for a list.
class ManuscriptLibraryDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ManuscriptLibraryDialog(QWidget *parent = nullptr);

    /// Absolute paths of what was chosen, in library order.
    QStringList chosenFiles() const;
    /// True when the editor asked to browse instead, so the caller falls back
    /// to the file dialog it used before the library existed.
    bool wantsToBrowse() const { return m_browse; }
    /// True when the library held nothing, so the caller can go straight to
    /// browsing rather than showing an empty list.
    bool isEmpty() const { return m_empty; }

private:
    QListWidget *m_list = nullptr;
    QPushButton *m_load = nullptr;
    bool m_browse = false;
    bool m_empty = false;
};

} // namespace milah
