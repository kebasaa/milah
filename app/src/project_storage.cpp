#include "project_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryFile>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#include <quazip/quazipnewinfo.h>

namespace {

bool writeEntry(
    QuaZip &archive,
    const QString &entryName,
    const QByteArray &contents,
    QString *errorMessage)
{
    QuaZipFile entry(&archive);
    QuaZipNewInfo info(entryName);
    if (!entry.open(QIODevice::WriteOnly, info)) {
        *errorMessage = QStringLiteral("Could not create %1 in the project.")
                            .arg(entryName);
        return false;
    }
    if (entry.write(contents) != contents.size()) {
        *errorMessage = QStringLiteral("Could not write %1 in the project.")
                            .arg(entryName);
        entry.close();
        return false;
    }
    entry.close();
    if (entry.getZipError() != 0) {
        *errorMessage = QStringLiteral("Could not finish %1 in the project.")
                            .arg(entryName);
        return false;
    }
    return true;
}

} // namespace

namespace milah {

bool ProjectStorage::isSafeEntryPath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(u'\\')) {
        return false;
    }
    const QString clean = QDir::cleanPath(path);
    return clean == path
        && clean != QStringLiteral(".")
        && !clean.startsWith(QStringLiteral("../"))
        && !clean.contains(QStringLiteral("/../"));
}

bool ProjectStorage::saveToPath(
    const QString &path,
    const QJsonObject &payload,
    QString *errorMessage,
    qint64 entryLimit)
{
    if (errorMessage == nullptr) {
        return false;
    }
    errorMessage->clear();

    const QJsonObject manifest = payload.value(QStringLiteral("manifest")).toObject();
    const QJsonArray files = payload.value(QStringLiteral("files")).toArray();
    if (manifest.isEmpty()) {
        *errorMessage = QStringLiteral("The project manifest is missing.");
        return false;
    }

    QTemporaryFile temporary(
        QFileInfo(path).absolutePath() + QStringLiteral("/milah-XXXXXX.tmp"));
    if (!temporary.open()) {
        *errorMessage = QStringLiteral("Could not create a temporary project file.");
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    QuaZip archive(temporaryPath);
    if (!archive.open(QuaZip::mdCreate)) {
        *errorMessage = QStringLiteral("Could not create the project archive.");
        return false;
    }

    const QByteArray manifestBytes =
        QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (!writeEntry(
            archive,
            QStringLiteral("manifest.json"),
            manifestBytes,
            errorMessage)) {
        archive.close();
        return false;
    }

    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QString entryPath = file.value(QStringLiteral("path")).toString();
        const QByteArray contents =
            QByteArray::fromBase64(
                file.value(QStringLiteral("contentBase64")).toString().toLatin1());
        if (!isSafeEntryPath(entryPath)) {
            *errorMessage = QStringLiteral("Unsafe project entry path: %1")
                                .arg(entryPath);
            archive.close();
            return false;
        }
        if (contents.size() > entryLimit) {
            *errorMessage = QStringLiteral("Project entry is too large: %1")
                                .arg(entryPath);
            archive.close();
            return false;
        }
        if (!writeEntry(archive, entryPath, contents, errorMessage)) {
            archive.close();
            return false;
        }
    }

    archive.close();
    if (archive.getZipError() != 0) {
        *errorMessage = QStringLiteral("Could not finish the project archive.");
        return false;
    }

    QFile completedArchive(temporaryPath);
    if (!completedArchive.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("Could not read the completed project.");
        return false;
    }
    const QByteArray archiveBytes = completedArchive.readAll();
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly)
        || destination.write(archiveBytes) != archiveBytes.size()
        || !destination.commit()) {
        *errorMessage = QStringLiteral("Could not save the project to %1.")
                            .arg(path);
        return false;
    }
    return true;
}

QJsonObject ProjectStorage::loadFromPath(
    const QString &path,
    QString *errorMessage,
    qint64 entryLimit)
{
    if (errorMessage == nullptr) {
        return {};
    }
    errorMessage->clear();

    QuaZip archive(path);
    if (!archive.open(QuaZip::mdUnzip)) {
        *errorMessage = QStringLiteral("Could not open the Milah project.");
        return {};
    }

    QJsonObject manifest;
    QJsonArray files;
    const QStringList entries = archive.getFileNameList();
    for (const QString &entryPath : entries) {
        if (!isSafeEntryPath(entryPath)) {
            *errorMessage = QStringLiteral("Unsafe path in project: %1")
                                .arg(entryPath);
            archive.close();
            return {};
        }
        if (!archive.setCurrentFile(entryPath)) {
            *errorMessage = QStringLiteral("Could not select %1.").arg(entryPath);
            archive.close();
            return {};
        }
        QuaZipFile entry(&archive);
        if (!entry.open(QIODevice::ReadOnly)) {
            *errorMessage = QStringLiteral("Could not read %1.").arg(entryPath);
            archive.close();
            return {};
        }
        const QByteArray contents = entry.read(entryLimit + 1);
        entry.close();
        if (contents.size() > entryLimit) {
            *errorMessage = QStringLiteral("Project entry is too large: %1")
                                .arg(entryPath);
            archive.close();
            return {};
        }

        if (entryPath == QStringLiteral("manifest.json")) {
            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(contents, &parseError);
            if (parseError.error != QJsonParseError::NoError
                || !document.isObject()) {
                *errorMessage = QStringLiteral("The project manifest is invalid.");
                archive.close();
                return {};
            }
            manifest = document.object();
        } else {
            files.append(QJsonObject{
                {QStringLiteral("path"), entryPath},
                {QStringLiteral("contentBase64"),
                 QString::fromLatin1(contents.toBase64())},
            });
        }
    }
    archive.close();

    if (manifest.isEmpty()) {
        *errorMessage = QStringLiteral("The project has no manifest.");
        return {};
    }
    return QJsonObject{
        {QStringLiteral("manifest"), manifest},
        {QStringLiteral("files"), files},
    };
}

} // namespace milah
