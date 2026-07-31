#pragma once

#include "core/types.h"

#include <QList>
#include <QString>
#include <QStringList>

class QJsonObject;

namespace milah {

/// One manuscript offered for download, as the published manifest describes it.
///
/// Everything here exists because a filename cannot carry it:
/// REV_Sloane237_hebrew_commented.osis does not say that it is Revelation from
/// British Library Sloane MS 237, written between 1500 and 1699, covering
/// 1:1–2:13. All of that lives in the OSIS header — which is precisely what a
/// download list cannot read, because choosing what to download is the point.
struct CatalogueEntry
{
    /// The name in the repository and on disk; the two are deliberately equal,
    /// so what was downloaded can be recognised without keeping a record.
    QString file;
    QString title;
    /// The OSIS book code the file is named for. Only used to group the list —
    /// the title is what is actually read.
    QString book;
    QString language;
    /// When the manuscript was written, as the transcriber gave it: "ca. 1730",
    /// "between 1500 and 1699", "undated". Free text, never parsed.
    QString date;
    /// What the edition covers. Absent from a third of the texts, so never
    /// assume it is there.
    QString covers;
    /// Which way Milah has to load it. The manifest states this rather than the
    /// app guessing, and it is why the library can load a file without asking.
    SourceRole role = SourceRole::Manuscript;
    qint64 bytes = 0;

    bool isTranslation() const { return role == SourceRole::Translation; }
};

/// What the published repository offers, read from its manifest.
class ManuscriptCatalogue
{
public:
    ManuscriptCatalogue() = default;

    /// Reads a manifest. An entry that cannot be understood is dropped rather
    /// than throwing, the way the phrase rules and the abbreviation table are
    /// read: one bad row must not cost the editor the whole catalogue.
    static ManuscriptCatalogue fromJson(const QJsonObject &document);

    const QList<CatalogueEntry> &entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.isEmpty(); }

    /// The file names of `directory` that this catalogue knows about, so the
    /// download window can say what is already held.
    QStringList installedFiles(const QString &directory) const;

private:
    QList<CatalogueEntry> m_entries;
};

/// Where downloaded manuscripts are looked for, most specific first:
/// $MILAH_MANUSCRIPT_DIR, then the writable folder below, then a `manuscripts`
/// folder beside the executable — which lets a portable copy ship texts that
/// need no download at all.
QStringList manuscriptSearchPaths();

/// Where a download is written. Under the application data directory rather
/// than beside the executable: an installed Milah lives in a folder its user
/// cannot write to, and a download that fails for everyone who did not install
/// portably is no feature at all.
QString manuscriptWriteDirectory();

/// Every OSIS file in the library, as absolute paths, nearest search path
/// first and each name taken only once.
QStringList installedManuscripts();

} // namespace milah
