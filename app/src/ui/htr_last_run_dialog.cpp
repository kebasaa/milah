#include "ui/htr_last_run_dialog.h"

#include "ui/kraken_environment.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace milah {
namespace {

/// Enough of a layout file to see its shape and its first words. A folio's ALTO
/// runs to a third of a megabyte, nearly all of it polygon coordinates, and a
/// text box asked to hold the whole of it helps nobody.
constexpr qint64 MostOfAFile = 200 * 1024;

QString readable(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return QStringLiteral("(not written)");
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("(could not be read: %1)").arg(file.errorString());
    }
    QString text = QString::fromUtf8(file.read(MostOfAFile));
    if (file.size() > MostOfAFile) {
        text += QStringLiteral("\n\n… %1 more, at %2")
                    .arg(QLocale().formattedDataSize(file.size() - MostOfAFile),
                         QDir::toNativeSeparators(path));
    }
    return text;
}

QPlainTextEdit *page(const QString &text)
{
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setPlainText(text);
    return view;
}

} // namespace

bool HtrLastRunDialog::hasRun()
{
    return QFileInfo::exists(
        QDir(KrakenEnvironment::lastRunDirectory()).filePath(QStringLiteral("command.txt")));
}

HtrLastRunDialog::HtrLastRunDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("The last recognition"));
    resize(820, 560);

    const QDir where(KrakenEnvironment::lastRunDirectory());

    auto *heading = new QLabel(
        QStringLiteral("Kept from the last time a folio was handed to Kraken, in "
                       "<code>%1</code>.")
            .arg(QDir::toNativeSeparators(where.path())));
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);

    auto *tabs = new QTabWidget;
    tabs->addTab(page(readable(where.filePath(QStringLiteral("command.txt")))),
                 QStringLiteral("What Milah ran"));
    tabs->addTab(page(readable(where.filePath(QStringLiteral("output.txt")))),
                 QStringLiteral("What Kraken said"));
    tabs->addTab(page(readable(where.filePath(QStringLiteral("folio.xml")))),
                 QStringLiteral("Layout file"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(heading);
    outer->addWidget(tabs, 1);
    outer->addWidget(buttons);
}

} // namespace milah
