#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Milah"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MILAH_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Milah"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/img/milah.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Offline manuscript-comparison editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption translationOption(
        QStringList{QStringLiteral("t"), QStringLiteral("translation")},
        QStringLiteral("Load an OSIS file as a translation rather than a manuscript."),
        QStringLiteral("file"));
    parser.addOption(translationOption);
    parser.addPositionalArgument(
        QStringLiteral("manuscript"),
        QStringLiteral("OSIS manuscript files to open."),
        QStringLiteral("[manuscript...]"));
    parser.process(application);

    milah::MainWindow window;
    // Maximised from the start: a verse card is a wide table of witnesses side
    // by side, and the band packing splits it into stacked bands as soon as the
    // window is narrow. Opening small would mean the first thing seen is the
    // most broken-up view of the text.
    window.showMaximized();
    window.openFiles(parser.positionalArguments(), parser.values(translationOption));

    return application.exec();
}
