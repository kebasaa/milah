#include "ui/dictionary_entry_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Tall enough for a few senses without the dialog becoming a document editor.
constexpr int DefinitionLines = 4;
/// The headword is the thing being defined, so it is set larger than the prose
/// about it — the same reasoning that sets readings above the interface size.
constexpr qreal HeadwordScale = 1.6;

} // namespace

DictionaryEntryDialog::DictionaryEntryDialog(
    const QString &word, const QStringList &definitions, QWidget *parent)
    : QDialog(parent)
{
    const bool editing = !definitions.isEmpty();
    setWindowTitle(
        editing ? QStringLiteral("Edit dictionary entry")
                : QStringLiteral("Add to my dictionary"));
    setObjectName(QStringLiteral("dictionaryEntryDialog"));

    auto *headword = new QLabel(word);
    headword->setObjectName(QStringLiteral("dictionaryHeadword"));
    QFont headwordFont = font();
    headwordFont.setPointSizeF(headwordFont.pointSizeF() * HeadwordScale);
    headword->setFont(headwordFont);
    // The word is Hebrew: it reads right to left even though the dialog around
    // it does not, and it is worth being able to copy into a note elsewhere.
    headword->setLayoutDirection(Qt::RightToLeft);
    headword->setAlignment(Qt::AlignCenter);
    headword->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_definitions = new QPlainTextEdit;
    m_definitions->setObjectName(QStringLiteral("dictionaryDefinitions"));
    m_definitions->setPlainText(definitions.join(QLatin1Char('\n')));
    // One per line is the only thing about this field a reader could not guess,
    // so it is the thing the placeholder says.
    m_definitions->setPlaceholderText(
        QStringLiteral("What the word means, one definition per line."));
    // English prose under a Hebrew headword, the way the interlinear gloss sits
    // under its reading.
    m_definitions->setLayoutDirection(Qt::LeftToRight);
    m_definitions->setFixedHeight(
        m_definitions->fontMetrics().lineSpacing() * DefinitionLines
        + m_definitions->frameWidth() * 2 + DefinitionLines);
    m_definitions->setFocus();

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(12);
    layout->addWidget(headword);
    layout->addWidget(m_definitions);
    layout->addWidget(buttons);

    setFixedWidth(420);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
}

QStringList DictionaryEntryDialog::definitions() const
{
    QStringList entered;
    const QStringList lines = m_definitions->toPlainText().split(QLatin1Char('\n'));
    entered.reserve(lines.size());
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            entered.append(trimmed);
        }
    }
    return entered;
}

} // namespace milah
