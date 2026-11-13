// milah-training: writes Kraken training data out of Milah transcriptions,
// without opening the application.
//
//   milah-training export --out <directory> [--hand <hand>] <file.trscrpt>...
//
// Each transcription's finished lines — those somebody has marked read — go
// under <directory>/<hand>/<manuscript>/ as ALTO beside the folio picture the
// file holds, which is what `ketos train -f alto` reads:
//
//   ketos train -f alto -i base.mlmodel <directory>/<hand>/*/*.xml
//
// Nothing is fetched from the network, and nothing is written into Milah's own
// training sets.

#include "ui/training_set.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

using namespace milah;

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("milah-training"));
#ifdef MILAH_VERSION
    QCoreApplication::setApplicationVersion(QStringLiteral(MILAH_VERSION));
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Writes Kraken training data (ALTO and folio pictures) out of Milah "
        "transcriptions. Only lines marked read are written."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("export"));
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Transcriptions (.trscrpt)."),
                                 QStringLiteral("<file.trscrpt>..."));
    const QCommandLineOption out(
        {QStringLiteral("o"), QStringLiteral("out")},
        QStringLiteral("Where to write: one folder per hand, one per manuscript inside it."),
        QStringLiteral("directory"));
    const QCommandLineOption hand(
        QStringLiteral("hand"),
        QStringLiteral("Only transcriptions in this hand (as named in their details)."),
        QStringLiteral("hand"));
    parser.addOption(out);
    parser.addOption(hand);
    parser.process(application);

    QTextStream err(stderr);
    QTextStream said(stdout);
    QStringList arguments = parser.positionalArguments();
    if (arguments.isEmpty() || arguments.takeFirst() != QLatin1String("export")) {
        err << "Usage: milah-training export --out <directory> [--hand <hand>] "
               "<file.trscrpt>...\n";
        return 2;
    }
    if (!parser.isSet(out) || arguments.isEmpty()) {
        err << "Give --out and at least one transcription.\n";
        return 2;
    }

    const QString directory = QDir(parser.value(out)).absolutePath();
    if (!QDir().mkpath(directory)) {
        err << "Cannot create " << directory << "\n";
        return 1;
    }

    const TrainingSet::ExportReport report =
        TrainingSet::exportTranscriptions(arguments, directory, parser.value(hand));
    for (const QString &line : report.said) {
        said << line << "\n";
    }
    said << report.lines << " line(s) off " << report.folios << " folio(s) written to "
         << directory << "\n";
    // Nothing written is worth a non-zero exit: a script calling this wants to
    // know it has nothing to train on before it starts an hour of training.
    return report.lines > 0 ? 0 : 1;
}
