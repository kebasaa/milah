#pragma once

#include <QJsonObject>
#include <QString>

namespace milah {

class ProjectStorage
{
public:
    static bool saveToPath(
        const QString &path,
        const QJsonObject &payload,
        QString *errorMessage);

    static QJsonObject loadFromPath(
        const QString &path,
        QString *errorMessage);

private:
    static bool isSafeEntryPath(const QString &path);
};

} // namespace milah
