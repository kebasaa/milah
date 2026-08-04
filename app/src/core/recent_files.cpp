#include "core/recent_files.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSettings>

namespace milah {
namespace {

/// The one spelling of a path this list stores and compares by: absolute, with
/// `.` and `..` resolved and separators made uniform.
///
/// QFileInfo::canonicalFilePath would be better and cannot be used: it answers
/// empty for a file that is not there, and half the point of this list is to
/// remember files that are not there at the moment.
QString settled(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/// Whether the filesystem tells two spellings of a name apart. Windows does
/// not, so "C:/Work/Rev.milah" and "c:/work/rev.milah" are one file and must be
/// one entry.
constexpr Qt::CaseSensitivity kNameCase =
#ifdef Q_OS_WIN
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

bool sameFile(const QString &left, const QString &right)
{
    return left.compare(right, kNameCase) == 0;
}

} // namespace

QStringList withRecentFile(const QStringList &existing, const QString &path, int limit)
{
    const QString wanted = settled(path);
    if (wanted.isEmpty() || limit <= 0) {
        return existing;
    }

    QStringList kept{wanted};
    for (const QString &entry : existing) {
        const QString candidate = settled(entry);
        if (candidate.isEmpty() || sameFile(candidate, wanted)) {
            // The file being remembered again: it moves to the head rather than
            // appearing twice, which is what makes this a list of files rather
            // than a list of openings.
            continue;
        }
        if (kept.size() == limit) {
            break;
        }
        kept.append(candidate);
    }
    return kept;
}

QStringList recentFileLabels(const QStringList &paths)
{
    QHash<QString, int> shares;
    for (const QString &path : paths) {
        shares[QFileInfo(path).fileName().toLower()] += 1;
    }

    QStringList labels;
    labels.reserve(paths.size());
    for (const QString &path : paths) {
        const QFileInfo file(path);
        if (shares.value(file.fileName().toLower()) < 2) {
            labels.append(file.fileName());
            continue;
        }
        // Named twice over, so each says which folder it is the one in. The
        // folder's own name rather than the whole path: the path is on the
        // tooltip, and a menu entry three directories wide is unreadable.
        const QString folder = file.absoluteDir().dirName();
        labels.append(
            folder.isEmpty()
                ? file.absoluteFilePath()
                : QStringLiteral("%1  —  %2").arg(file.fileName(), folder));
    }
    return labels;
}

QStringList recentFiles(const QString &key)
{
    return QSettings().value(key).toStringList();
}

void rememberRecentFile(const QString &key, const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(key, withRecentFile(settings.value(key).toStringList(), path));
}

void forgetRecentFile(const QString &key, const QString &path)
{
    const QString unwanted = settled(path);
    if (unwanted.isEmpty()) {
        return;
    }

    QSettings settings;
    const QStringList existing = settings.value(key).toStringList();
    QStringList kept;
    kept.reserve(existing.size());
    for (const QString &entry : existing) {
        if (!sameFile(settled(entry), unwanted)) {
            kept.append(entry);
        }
    }
    settings.setValue(key, kept);
}

void clearRecentFiles(const QString &key)
{
    QSettings().remove(key);
}

} // namespace milah
