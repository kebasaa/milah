#include "core/manuscript_catalogue.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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

/// What a translation's title leads with, longest first so "English Translation
/// of" is not left as a stray "English" by matching the shorter one.
const QStringList &translationLeads()
{
    static const QStringList leads = {
        QStringLiteral("English Translation of "),
        QStringLiteral("Translation of "),
    };
    return leads;
}

} // namespace

QString CatalogueEntry::displayTitle() const
{
    if (!isTranslation()) {
        return title;
    }

    QString named = title;
    for (const QString &lead : translationLeads()) {
        if (named.startsWith(lead, Qt::CaseInsensitive)) {
            named = named.mid(lead.size());
            break;
        }
    }

    // A title that was nothing but the lead would leave an empty row, which
    // says less than the doubled wording it was meant to improve on.
    if (named.trimmed().isEmpty()) {
        named = title;
    }

    return QStringLiteral("%1  (English translation)").arg(named);
}

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
        // Trimmed rather than taken raw, the same way rights is: these come out
        // of prose in an OSIS header and a stray newline in a table column is
        // a row that no longer lines up with the ones around it.
        entry.shelfmark = record.value(QStringLiteral("shelfmark")).toString().trimmed();
        entry.folios = record.value(QStringLiteral("folios")).toString().trimmed();
        entry.translatedFrom =
            record.value(QStringLiteral("translatedFrom")).toString().trimmed();
        entry.translationCertainty = record.value(QStringLiteral("translationCertainty"))
                                         .toString()
                                         .trimmed()
                                         .toLower();
        entry.exemplar = record.value(QStringLiteral("exemplar")).toString().trimmed();
        // Trimmed because the OSIS headers wrap these across lines, and a
        // tooltip that opens on a newline reads as broken.
        entry.rights = record.value(QStringLiteral("rights")).toString().trimmed();
        entry.license = record.value(QStringLiteral("license")).toString().trimmed();
        // Only used to say how large a download will be, so a size that makes
        // no sense costs a line of display, not the manuscript.
        entry.bytes = record.value(QStringLiteral("bytes")).toInteger(0);
        if (entry.bytes < 0) {
            entry.bytes = 0;
        }
        // Absent from a manifest written before checksums existed. Such an
        // entry is still perfectly downloadable; it simply cannot report that
        // an update is waiting.
        entry.sha256 =
            record.value(QStringLiteral("sha256")).toString().trimmed().toLower();

        catalogue.m_entries.append(entry);
    }

    return catalogue;
}

QString translationSubType(const QString &certainty)
{
    return certainty.isEmpty() ? QString() : QStringLiteral("x-%1").arg(certainty);
}

QString translationCertaintyOf(const QString &subType)
{
    static const QStringList known = {
        QLatin1String(TranslationCertainty::Certain),
        QLatin1String(TranslationCertainty::Uncertain),
        QLatin1String(TranslationCertainty::Original),
        QLatin1String(TranslationCertainty::OriginalUncertain),
    };
    if (!subType.startsWith(QLatin1String("x-"))) {
        return QString();
    }
    const QString value = subType.mid(2).toLower();
    // Anything else is a header saying something this version has never heard
    // of, which is not the same as saying nothing — but it is the same to a
    // reader, and inventing a column for it would be worse than a dash.
    return known.contains(value) ? value : QString();
}

QString translationColumn(const CatalogueEntry &entry)
{
    const QString certainty = entry.translationCertainty;
    if (certainty.isEmpty()) {
        // Not a claim that it is original, and not a claim that it is not:
        // nobody has recorded an answer, and the column says exactly that.
        return QStringLiteral("—");
    }

    const bool settled = certainty == QLatin1String(TranslationCertainty::Certain)
        || certainty == QLatin1String(TranslationCertainty::Original);
    const bool original = certainty == QLatin1String(TranslationCertainty::Original)
        || certainty == QLatin1String(TranslationCertainty::OriginalUncertain);
    const QString mark = settled ? QString() : QStringLiteral("?");

    if (original) {
        return QStringLiteral("Original") + mark;
    }

    // What it renders, cut to the answer. The leading words are the same on
    // every one of them, and the whole sentence is on the tooltip.
    static const QStringList leads = {
        QStringLiteral("Translated from the "),
        QStringLiteral("Translated from "),
        QStringLiteral("A translation of the "),
        QStringLiteral("A translation of "),
    };
    QString source = entry.translatedFrom;
    for (const QString &lead : leads) {
        if (source.startsWith(lead, Qt::CaseInsensitive)) {
            source = source.mid(lead.size());
            break;
        }
    }
    source = source.trimmed();
    if (source.endsWith(QLatin1Char('.'))) {
        source.chop(1);
    }
    if (source.isEmpty()) {
        // A verdict with nothing named: it is a rendering of something nobody
        // has written down.
        return QStringLiteral("Yes") + mark;
    }
    constexpr int room = 26;
    if (source.size() > room) {
        source = source.left(room - 1).trimmed() + QStringLiteral("…");
    }
    return source + mark;
}

QString ManuscriptCatalogue::manuscriptAge(const CatalogueEntry &entry) const
{
    if (!entry.isTranslation() || entry.shelfmark.isEmpty()) {
        return entry.date;
    }

    // The witness this renders, by the shelfmark they share — the one field
    // that names the physical object rather than the file. Its date is the
    // manuscript's age; this entry's own is the year somebody translated it.
    for (const CatalogueEntry &witness : m_entries) {
        if (!witness.isTranslation() && witness.shelfmark == entry.shelfmark
            && !witness.date.isEmpty()) {
            return witness.date;
        }
    }
    return entry.date;
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

QStringList ManuscriptCatalogue::updatableFiles(const QString &directory) const
{
    QStringList stale;
    if (directory.isEmpty()) {
        return stale;
    }

    const QDir folder(directory);
    for (const CatalogueEntry &entry : m_entries) {
        if (entry.sha256.isEmpty()) {
            continue;
        }
        const QFileInfo held(folder, entry.file);
        if (!held.isReadable()) {
            continue;
        }
        const QString actual = manuscriptChecksum(held.absoluteFilePath());
        // An unreadable file is not reported as stale: that is a different
        // problem and re-downloading would not be an answer to it.
        if (!actual.isEmpty() && actual != entry.sha256) {
            stale.append(entry.file);
        }
    }
    return stale;
}

QString manuscriptChecksum(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    // Read whole rather than streamed: the largest published text is under a
    // quarter of a megabyte, and normalising line endings across chunk
    // boundaries would be the only fiddly part of doing it incrementally.
    QByteArray contents = file.readAll();
    contents.replace("\r\n", "\n");

    return QString::fromLatin1(
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
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
