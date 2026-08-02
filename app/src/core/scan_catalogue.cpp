#include "core/scan_catalogue.h"

#include <QJsonArray>
#include <QJsonObject>

namespace milah {
namespace {

QString text(const QJsonObject &record, const char *key)
{
    return record.value(QLatin1String(key)).toString().trimmed();
}

} // namespace

QString ScanEntry::displayTitle() const
{
    if (shelfmark.isEmpty()) {
        return title;
    }
    // Two libraries hold a "Hebrew translation of the New Testament"; only one
    // holds MS Oo.1.32.
    return QStringLiteral("%1 (%2)").arg(title, shelfmark);
}

ScanCatalogue ScanCatalogue::fromJson(const QJsonObject &document)
{
    ScanCatalogue catalogue;

    const QJsonArray scans = document.value(QStringLiteral("scans")).toArray();
    for (const QJsonValue &value : scans) {
        const QJsonObject record = value.toObject();

        ScanEntry entry;
        entry.id = text(record, "id");
        if (entry.id.isEmpty()) {
            // Nothing to tell this scan from another by, which the picker needs
            // and the transcription records.
            continue;
        }

        entry.title = text(record, "title");
        entry.shelfmark = text(record, "shelfmark");
        entry.repository = text(record, "repository");
        entry.origin = text(record, "origin");
        entry.date = text(record, "date");
        entry.language = text(record, "language");
        entry.material = text(record, "material");
        entry.provenance = text(record, "provenance");
        entry.attribution = text(record, "attribution");
        entry.licence = text(record, "licence");
        entry.book = text(record, "book");
        entry.folios = text(record, "folios");
        entry.source = text(record, "source");
        entry.unavailable = text(record, "unavailable");
        if (entry.title.isEmpty()) {
            // A scan with no title can still be opened; it is named by what it
            // is kept under, or failing that by its identifier.
            entry.title = entry.shelfmark.isEmpty() ? entry.id : entry.shelfmark;
        }

        const QJsonArray pages = record.value(QStringLiteral("pages")).toArray();
        entry.pages.reserve(pages.size());
        for (const QJsonValue &pageValue : pages) {
            const QJsonObject pageRecord = pageValue.toObject();

            ScanPage page;
            page.imageUrl = text(pageRecord, "image");
            if (page.imageUrl.isEmpty()) {
                // A folio nobody can fetch is a blank page with nothing to say
                // why, so it is not offered at all.
                continue;
            }
            page.label = text(pageRecord, "label");
            // Counted from the manifest where it says, and from position where
            // it does not: a folio's place is how the arrows walk the codex,
            // and a run of zeroes would put them all in the same place.
            page.number = pageRecord.value(QStringLiteral("n")).toInt(entry.pages.size() + 1);
            entry.pages.append(page);
        }

        if (entry.pages.isEmpty() && entry.unavailable.isEmpty()) {
            // A resolver that found nothing to fetch — not a manuscript
            // recorded as unavailable on purpose, which is kept and shown as
            // such rather than made to look like it does not exist.
            continue;
        }
        catalogue.m_entries.append(entry);
    }

    return catalogue;
}

} // namespace milah
