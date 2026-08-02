#pragma once

#include <QJsonObject>
#include <QString>

namespace milah {

class ProjectStorage
{
public:
    /// The largest single file the archive will carry.
    ///
    /// A guard against a malformed or hostile archive claiming to hold more
    /// than memory, not a statement about what a document may contain — so it
    /// is per document type rather than fixed. An edition is OSIS text and has
    /// never come near this; a transcription carries folio photographs and
    /// wants a great deal more room, which is why it is a parameter at all.
    static constexpr qint64 EditionEntryLimit = 64 * 1024 * 1024;
    static constexpr qint64 ImageEntryLimit = 512 * 1024 * 1024;

    static bool saveToPath(
        const QString &path,
        const QJsonObject &payload,
        QString *errorMessage,
        qint64 entryLimit = EditionEntryLimit);

    static QJsonObject loadFromPath(
        const QString &path,
        QString *errorMessage,
        qint64 entryLimit = EditionEntryLimit);

private:
    static bool isSafeEntryPath(const QString &path);
};

} // namespace milah
