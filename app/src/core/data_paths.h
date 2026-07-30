#pragma once

#include <QString>
#include <QStringList>

namespace milah {

/// The directories Milah reads its data files from, in the order it tries
/// them. Highest priority first, so a replacement can be dropped in without
/// disturbing the installed copy.
///
///   1. `$MILAH_DATA_DIR`, when set — for pointing at a freshly generated file
///   2. `data/` under the user's application data directory — a personal copy
///   3. `data/` beside the executable — what ships with the application
///
/// The files are read rather than compiled in, so a newer lexicon can replace
/// an older one without rebuilding Milah.
QStringList dataSearchPaths();

/// The first readable copy of `fileName`, or an empty string when no search
/// path holds one.
QString locateDataFile(const QString &fileName);

} // namespace milah
