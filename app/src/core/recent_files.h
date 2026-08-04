#pragma once

#include <QString>
#include <QStringList>

namespace milah {

/// How many files a File menu offers under "Open Recent". Five is about where a
/// list stops being a shortcut and starts being a directory listing.
inline constexpr int RecentFileLimit = 5;

/// Where each tab's list is kept, under the same default QSettings the rest of
/// the application uses. Two lists rather than one: a `.milah` edition and a
/// `.trscrpt` transcription are two kinds of work, and neither tab can open the
/// other's files.
inline constexpr char RecentProjectsKey[] = "recent/projects";
inline constexpr char RecentTranscriptionsKey[] = "recent/transcriptions";

/// `existing` with `path` at its head: most recent first, no duplicates, and no
/// more than `limit` of them. The oldest falls off the end.
///
/// Paths are cleaned and made absolute before they are compared, and compared
/// without regard to case where the filesystem does not have any — otherwise
/// one file opened once through a dialog and once from this list would sit in
/// it twice, and the second copy would push something real off the bottom.
QStringList withRecentFile(
    const QStringList &existing, const QString &path, int limit = RecentFileLimit);

/// What to call each of `paths` in a menu, in the same order.
///
/// A file's own name, and — only for the ones that would otherwise read alike —
/// the folder holding it as well. Manuscripts are filed by book, so two of them
/// are very likely to hold a "Revelation.milah" apiece, and a menu offering the
/// same word twice is one nobody can choose from.
QStringList recentFileLabels(const QStringList &paths);

/// The list under `key`, most recent first. Files that have since been deleted
/// are still in it: a project on a drive that is not plugged in has not been
/// abandoned, and comes back when the drive does.
QStringList recentFiles(const QString &key);

/// Puts `path` at the head of the list under `key`. Called wherever a file is
/// successfully opened *or* written, since a file just saved is one about to be
/// wanted again.
void rememberRecentFile(const QString &key, const QString &path);

/// Takes `path` out of the list under `key`. For a file that would not open:
/// having just failed somebody, it should not still be on offer.
void forgetRecentFile(const QString &key, const QString &path);

void clearRecentFiles(const QString &key);

} // namespace milah
