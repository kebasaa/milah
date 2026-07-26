#include "web_bridge.h"

#include "project_storage.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QWidget>

namespace {

constexpr qint64 MaxOsisSize = 64 * 1024 * 1024;

}

namespace milah {

WebBridge::WebBridge(QWidget *dialogParent)
    : QObject(dialogParent)
    , m_dialogParent(dialogParent)
{
}

QString WebBridge::lastError() const
{
    return m_lastError;
}

void WebBridge::setLastError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

QVariantList WebBridge::openOsisFiles(const QString &role)
{
    setLastError({});
    const QString title = role == QStringLiteral("translation")
        ? QStringLiteral("Load translation OSIS files")
        : QStringLiteral("Load manuscript OSIS files");
    const QStringList paths = QFileDialog::getOpenFileNames(
        m_dialogParent,
        title,
        {},
        QStringLiteral("OSIS files (*.osis *.xml);;All files (*)"));

    QVariantList result;
    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            setLastError(QStringLiteral("Could not read %1.").arg(path));
            return {};
        }
        const QByteArray contents = file.read(MaxOsisSize + 1);
        if (contents.size() > MaxOsisSize) {
            setLastError(QStringLiteral("OSIS file is too large: %1").arg(path));
            return {};
        }
        result.append(QVariantMap{
            {QStringLiteral("name"), QFileInfo(path).fileName()},
            {QStringLiteral("path"), path},
            {QStringLiteral("role"), role},
            {QStringLiteral("content"), QString::fromUtf8(contents)},
        });
    }
    return result;
}

QVariantMap WebBridge::openProject()
{
    setLastError({});
    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Open Milah project"),
        {},
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        return {};
    }
    QString error;
    const QJsonObject payload = ProjectStorage::loadFromPath(path, &error);
    if (!error.isEmpty()) {
        setLastError(error);
        return {};
    }
    QVariantMap result = payload.toVariantMap();
    result.insert(QStringLiteral("projectPath"), path);
    return result;
}

bool WebBridge::saveProject(const QVariantMap &payload)
{
    setLastError({});
    const QString suggested =
        payload.value(QStringLiteral("suggestedName"), QStringLiteral("project.milah"))
            .toString();
    const QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Save Milah project"),
        suggested,
        QStringLiteral("Milah projects (*.milah)"));
    if (path.isEmpty()) {
        return false;
    }

    QString finalPath = path;
    if (!finalPath.endsWith(QStringLiteral(".milah"), Qt::CaseInsensitive)) {
        finalPath += QStringLiteral(".milah");
    }
    QJsonObject project = QJsonObject::fromVariantMap(payload);
    project.remove(QStringLiteral("suggestedName"));
    QString error;
    if (!ProjectStorage::saveToPath(finalPath, project, &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool WebBridge::exportCombinedOsis(const QString &osis)
{
    setLastError({});
    QString path = QFileDialog::getSaveFileName(
        m_dialogParent,
        QStringLiteral("Export Combined OSIS"),
        QStringLiteral("Milah_Combined.osis"),
        QStringLiteral("OSIS files (*.osis *.xml)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".osis"), Qt::CaseInsensitive)
        && !path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".osis");
    }

    QSaveFile file(path);
    const QByteArray contents = osis.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
        || file.write(contents) != contents.size()
        || !file.commit()) {
        setLastError(QStringLiteral("Could not export Combined OSIS."));
        return false;
    }
    return true;
}

} // namespace milah
