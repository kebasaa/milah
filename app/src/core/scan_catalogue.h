#pragma once

#include <QList>
#include <QString>

class QJsonObject;

namespace milah {

/// One folio of a published scan: where its image lives, and what the library
/// calls it.
///
/// The label is worth carrying because libraries name folios the way a reader
/// does — "front cover", "1r", "1v", "162v" — and that is exactly the name the
/// transcription window wants for a page. Milah has nothing else to call it: a
/// scan has no filenames.
struct ScanPage
{
    /// Where the folio sits in the manuscript, as the library counts.
    int number = 0;
    QString label;
    /// Absolute, and already carrying whatever the image server needs to be
    /// asked for a picture — the manifest generator settles the size and the
    /// format, so nothing here has to know one library's syntax from another's.
    QString imageUrl;
};

/// A manuscript somebody has photographed and published, as the scan manifest
/// describes it.
///
/// Everything but the folio list is what the holding library says about the
/// codex, carried so a transcriber does not have to copy it out of a web page:
/// opening a scan offers to fill the Manuscript panel from it.
struct ScanEntry
{
    QString id;
    QString title;
    QString shelfmark;
    QString repository;
    QString origin;
    /// As the library gives it — "Eighteenth century", "between 1500 and 1699".
    /// Free text, never parsed.
    QString date;
    QString language;
    QString material;
    QString provenance;
    /// Who to credit, and on what terms the images may be used. Both travel
    /// with the scan because the images stay on the library's own server under
    /// the library's own licence, which is not this application's to grant.
    QString attribution;
    QString licence;
    /// The OSIS book code, where the scan is of one book. Empty for a codex
    /// covering many, which most are.
    QString book;
    /// Which part of the codex this is, as the library labels its folios —
    /// "22r..33v". Empty where the entry is the whole of it. One binding may be
    /// offered whole and book by book at once, and then several entries share a
    /// shelfmark and differ only here.
    QString folios;
    /// The viewer page a reader would open to see this themselves. Also what
    /// says that two entries are of one manuscript, which is how the picker
    /// knows to gather them under it.
    QString source;
    /// Why there is nothing to open, or empty when there is something.
    ///
    /// A manuscript known to exist but never photographed — or not currently
    /// servable — still belongs in the picker: a transcriber should not have
    /// to already know it is missing to learn that it is. Non-empty is what
    /// tells fromJson() to keep an entry with no pages rather than drop it as
    /// malformed, and what tells the picker to show it disabled rather than
    /// offer a folio there is nothing behind.
    QString unavailable;
    QList<ScanPage> pages;

    /// The scan as the picker should name it: the title with its shelfmark
    /// after it, because two libraries hold manuscripts with the same title and
    /// the shelfmark is what tells them apart.
    QString displayTitle() const;
};

/// What can be transcribed without downloading anything first, read from the
/// published scan manifest.
class ScanCatalogue
{
public:
    ScanCatalogue() = default;

    /// Reads a manifest. An entry that cannot be understood is dropped rather
    /// than throwing, the same contract ManuscriptCatalogue keeps: one bad row
    /// must not cost the transcriber the whole catalogue.
    ///
    /// A scan with no folios is dropped — there would be nothing to open — and
    /// so is a folio with no image address, since offering it would put a blank
    /// page on screen with nothing to say why. The one exception is an entry
    /// whose ``unavailable`` field is not empty: a manuscript recorded as
    /// having no scan at all is kept, with no pages, so it can be shown rather
    /// than made invisible.
    static ScanCatalogue fromJson(const QJsonObject &document);

    const QList<ScanEntry> &entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.isEmpty(); }

private:
    QList<ScanEntry> m_entries;
};

} // namespace milah
