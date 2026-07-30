#include "core/data_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace milah {

QStringList dataSearchPaths()
{
    QStringList paths;

    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("MILAH_DATA_DIR"));
    if (!override.isEmpty()) {
        paths.append(override);
    }

    const QString personal =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!personal.isEmpty()) {
        paths.append(personal + QStringLiteral("/data"));
    }

    // QCoreApplication may not exist yet in a bare unit test; the executable's
    // own directory is only knowable once it does.
    if (QCoreApplication::instance()) {
        const QString beside = QCoreApplication::applicationDirPath();
        paths.append(beside + QStringLiteral("/data"));
        // A portable layout that puts the binary in bin/ keeps data alongside.
        paths.append(beside + QStringLiteral("/../data"));
    }

    return paths;
}

QString locateDataFile(const QString &fileName)
{
    for (const QString &directory : dataSearchPaths()) {
        const QFileInfo candidate(QDir(directory), fileName);
        if (candidate.isReadable()) {
            return candidate.absoluteFilePath();
        }
    }
    return QString();
}

} // namespace milah
