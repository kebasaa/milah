#pragma once

#include <QDialog>
#include <QStringList>

class QPlainTextEdit;

namespace milah {

/// Asks what a word means before it is accepted into the editor's dictionary.
///
/// Doubles as the edit path: constructed with the definitions a word already
/// carries, it shows them for correcting, reordering or removing. Accepting
/// with an empty field is allowed and means "I accept this word but have
/// nothing to say about it" — the state every word accepted before this dialog
/// existed is in.
class DictionaryEntryDialog final : public QDialog
{
    Q_OBJECT

public:
    DictionaryEntryDialog(
        const QString &word,
        const QStringList &definitions,
        QWidget *parent = nullptr);

    /// One per line, trimmed, blanks dropped.
    QStringList definitions() const;

private:
    QPlainTextEdit *m_definitions = nullptr;
};

} // namespace milah
