#include "core/manuscript_catalogue.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>

namespace milah {
namespace {

/// The folder downloads live in, under whichever root is being used.
const QString &libraryFolder()
{
    static const QString name = QStringLiteral("manuscripts");
    return name;
}

} // namespace

ManuscriptCatalogue ManuscriptCatalogue::fromJson(const QJsonObject &document)
{
    ManuscriptCatalogue catalogue;

    for (const QJsonValue &value :
         document.value(QStringLiteral("manuscripts")).toArray()) {
        const QJsonObject record = value.toObject();

        CatalogueEntry entry;
        entry.file = record.value(QStringLiteral("file")).toString();
        if (entry.file.isEmpty()) {
            // Without a name there is nothing to fetch and nothing to
            // recognise later.
            continue;
        }

        // The role decides how Milah loads the file, so a value it does not
        // understand is not something to guess at.
        const QString role = record.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("translation")) {
            entry.role = SourceRole::Translation;
        } else if (role == QStringLiteral("manuscript")) {
            entry.role = SourceRole::Manuscript;
        } else {
            continue;
        }

        entry.title = record.value(QStringLiteral("title")).toString();
        if (entry.title.isEmpty()) {
            // A nameless row is still worth offering; the file name at least
            // tells a reader which manuscript it is.
            entry.title = entry.file;
        }
        entry.book = record.value(QStringLiteral("book")).toString();
        entry.language = record.value(QStringLiteral("language")).toString();
        entry.date = record.value(QStringLiteral("date")).toString();
        // Absent from a third of the published texts, and only ever displayed.
        entry.covers = record.value(QStringLiteral("covers")).toString();
        // Only used to say how large a download will be, so a size that makes
        // no sense costs a line of display, not the manuscript.
        entry.bytes = record.value(QStringLiteral("bytes")).toInteger(0);
        if (entry.bytes < 0) {
            entry.bytes = 0;
        }

        catalogue.m_entries.append(entry);
    }

    return catalogue;
}

QStringList ManuscriptCatalogue::installedFiles(const QString &directory) const
{
    QStringList held;
    if (directory.isEmpty()) {
        return held;
    }

    const QDir folder(directory);
    for (const CatalogueEntry &entry : m_entries) {
        if (QFileInfo(folder, entry.file).isReadable()) {
            held.append(entry.file);
        }
    }
    return held;
}

QStringList manuscriptSearchPaths()
{
    QStringList paths;

    // The override is what lets the download and library be exercised against a
    // local copy before anything is published, and what lets an institution
    // point Milah at its own set. Same idea as MILAH_DATA_DIR.
    const QString override = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("MILAH_MANUSCRIPT_DIR"));
    if (!override.isEmpty()) {
        paths.append(override);
    }

    const QString personal = manuscriptWriteDirectory();
    if (!personal.isEmpty()) {
        paths.append(personal);
    }

    // QCoreApplication may not exist yet in a bare unit test; the executable's
    // own directory is only knowable once it does.
    if (QCoreApplication::instance()) {
        paths.append(
            QCoreApplication::applicationDirPath() + QLatin1Char('/') + libraryFolder());
    }

    return paths;
}

QString manuscriptWriteDirectory()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        return QString();
    }
    return directory + QLatin1Char('/') + libraryFolder();
}

QStringList installedManuscripts()
{
    QStringList found;
    QSet<QString> seen;

    for (const QString &directory : manuscriptSearchPaths()) {
        const QDir folder(directory);
        if (!folder.exists()) {
            continue;
        }
        const QStringList names =
            folder.entryList({QStringLiteral("*.osis")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            // Nearest path wins: a text the editor downloaded shadows one
            // shipped beside the binary, rather than appearing twice.
            if (seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            found.append(folder.absoluteFilePath(name));
        }
    }

    return found;
}

} // namespace milah
