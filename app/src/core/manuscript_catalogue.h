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
    /// The `<rights>` line from the OSIS header: who holds the copyright and on
    /// what terms the text may be used.
    ///
    /// Shown before anything is downloaded, because the terms are not uniform
    /// and not all of them are permissive — some of these translations are
    /// "All rights reserved" while the transcriptions beside them are CC
    /// BY-NC-SA. A reader is entitled to know which of the two they are taking
    /// a copy of, and telling them afterwards is telling them too late.
    ///
    /// Empty when the manifest predates the field, in which case the window
    /// says nothing rather than implying no terms apply.
    QString rights;
    /// Which way Milah has to load it. The manifest states this rather than the
    /// app guessing, and it is why the library can load a file without asking.
    SourceRole role = SourceRole::Manuscript;
    qint64 bytes = 0;
    /// Checksum of the published file, lowercase hex. Empty when the manifest
    /// predates checksums, in which case the text can still be downloaded but
    /// nothing can be said about whether the copy held is current.
    QString sha256;

    bool isTranslation() const { return role == SourceRole::Translation; }

    /// The title as the download window should show it.
    ///
    /// A translation's own title already says what it is — "English Translation
    /// of Luke (Vatican, Vat. ebr. 530)" — so marking the row as a translation
    /// as well says it twice, and the two halves disagree about wording. This
    /// takes the lead off and puts one consistent mark at the end, so a
    /// translation sits under the same name as the witness it renders and the
    /// pair reads as a pair.
    ///
    /// The transcribers write the lead two ways ("Translation of" and "English
    /// Translation of"), and a third is only a matter of time, so a title
    /// carrying no lead at all is left alone rather than mangled.
    QString displayTitle() const;
};

/// The checksum of a file as the manifest measures it: over the LF form of its
/// contents, which is what GitHub serves and therefore what was downloaded.
///
/// Normalising matters because `.gitattributes` in the manuscripts repository
/// sets `* text=auto`, so a clone made on Windows with core.autocrlf may hold
/// CRLF where the server has LF. Hashing the raw bytes would then disagree with
/// the manifest for every file at once, and no download would ever settle it.
///
/// Empty when the file cannot be read.
QString manuscriptChecksum(const QString &path);

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

    /// Of those, the ones whose contents no longer match the manifest — a text
    /// that has been corrected since it was downloaded.
    ///
    /// A file that is not held is not updatable: "you do not have this" and
    /// "yours is out of date" are different answers and the window shows them
    /// differently. An entry with no checksum is never updatable either, since
    /// there is nothing to compare and claiming otherwise would offer an
    /// endless update.
    QStringList updatableFiles(const QString &directory) const;

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
