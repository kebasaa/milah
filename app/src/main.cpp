#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Milah"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MILAH_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Milah"));

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
    window.show();
    window.openFiles(parser.positionalArguments(), parser.values(translationOption));

    return application.exec();
}
