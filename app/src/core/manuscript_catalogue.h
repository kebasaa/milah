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

    // What a cataloguer looks a manuscript up by, and what the download window
    // shows in the columns beside its title. All four describe the manuscript
    // rather than the file, so a translation carries the same answers as the
    // witness it renders — it is a rendering of the same physical object.
    //
    // Every one of them may be empty, and usually is. These are questions
    // answered for some manuscripts and not others, and a catalogue that
    // insisted on them would be a catalogue nobody could add to.

    /// What the holding library files it under: "British Library, Sloane MS
    /// 237".
    QString shelfmark;
    /// Which leaves of the codex it occupies: "1r–4v".
    QString folios;
    /// What the Hebrew was rendered out of, where it is a rendering at all:
    /// "Translated from the Greek".
    ///
    /// Empty means nobody has recorded an answer. What the answer is, and how
    /// firmly it is held, are two separate questions — see translationCertainty.
    QString translatedFrom;
    /// How well the answer above is established. One of `TranslationCertainty`,
    /// or empty where nothing has been recorded at all.
    ///
    /// Kept apart from the prose because a column printing "Greek" flat would
    /// state as a fact what, for several of these manuscripts, is the very thing
    /// under argument. An explicit "original" is also what finally lets a Hebrew
    /// composition say it is one, rather than looking like a blank.
    QString translationCertainty;
    /// The older manuscript this one copies, where it is known to copy one:
    /// "Copied from Cambridge MS Oo.1.32".
    QString exemplar;
    // Who holds the text, and what a reader may do with it. Two questions with
    // two answers, kept apart because conflating them cannot describe the
    // published set: a copyright holder is named for every one of these texts,
    // and the terms range from "All rights reserved" through "free for any
    // non-commercial project" to CC BY-NC-SA. One field could only ever have
    // given one of those.
    //
    // Both are shown before anything is downloaded. A reader is entitled to
    // know what they are taking a copy of, and telling them afterwards is
    // telling them too late.

    /// Who holds the copyright, from `<rights type="x-copyright">`. May be
    /// legitimately empty, where nobody in particular is credited with a bare
    /// transcription.
    QString rights;
    /// On what terms it may be used, from `<rights type="x-license">`.
    ///
    /// Empty when the manifest predates the field, in which case the window
    /// says nothing rather than implying no terms apply. Silence is not a
    /// licence, and must not be shown as one.
    QString license;
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

/// What a catalogue may say about whether a Hebrew text renders another.
///
/// Two questions crossed: is it a rendering, and is that settled. Written out
/// as strings rather than an enum because they cross the manifest, the OSIS
/// headers and the archive as strings, and one spelling in one place is what
/// keeps the three agreeing.
namespace TranslationCertainty {
inline constexpr char Certain[] = "certain";
inline constexpr char Uncertain[] = "uncertain";
/// Not a rendering at all: an original Hebrew composition.
inline constexpr char Original[] = "original";
inline constexpr char OriginalUncertain[] = "original-uncertain";
} // namespace TranslationCertainty

/// The `subType` a certainty is written as in an OSIS header, or empty for one
/// nobody has recorded. The `x-` prefix is the schema's rule for a value it does
/// not itself define.
QString translationSubType(const QString &certainty);

/// The certainty an OSIS `subType` means, with its `x-` taken off. Empty for an
/// absent or unrecognised one, which are the same thing to a reader.
QString translationCertaintyOf(const QString &subType);

/// What the Translated from column shows: "Greek", "Greek?", "Original",
/// "Original?", or "—" where nothing has been recorded.
///
/// The prose is cut to its answer — "Translated from the Greek" is what a header
/// says and "Greek" is what fits beside six other columns — and a question mark
/// is what marks the unsettled ones. The whole sentence stays on the tooltip.
QString translationColumn(const CatalogueEntry &entry);

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

    /// How old the manuscript `entry` is a text of.
    ///
    /// Its own `date` for a witness. For a translation, the date of the witness
    /// it renders, found by shelfmark — because a translation's own date is the
    /// year somebody translated it, and a column headed Age that answered 2017
    /// for a manuscript written between 1500 and 1699 would be worse than a
    /// column that answered nothing.
    ///
    /// Falls back to the entry's own date when no witness can be found, which
    /// is at least a date and is marked as the file's own by nothing else
    /// claiming otherwise.
    QString manuscriptAge(const CatalogueEntry &entry) const;

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
