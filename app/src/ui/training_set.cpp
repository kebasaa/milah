#include "ui/training_set.h"

#include "core/training_export.h"
#include "core/transcription.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QStandardPaths>

namespace milah {
namespace TrainingSet {
namespace {

/// What a set is called on screen, kept beside its files.
///
/// The slug cannot say it: "MSOo132" is a folder name, and the transcriber
/// wrote "MS Oo.1.32". One small file rather than a settings key, so a set that
/// is copied to another machine arrives knowing its own name.
constexpr char kAbout[] = "about.json";

QString root()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("htr-training"));
}

QSize sizeOfImage(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return QSize();
    }
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    return reader.size();
}

/// How many `<TextLine>`s a saved file holds.
///
/// Counted out of the file rather than remembered in a tally, so a folder
/// somebody has tidied by hand still reports what is actually in it.
int linesIn(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    return int(file.readAll().count(QByteArray("<TextLine ")));
}

bool write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.commit();
}

/// The stem a folio's files are filed under, so add() and savedFolio() cannot
/// disagree about which folio is which.
QString stemFor(const TranscribedPage &page)
{
    const QString label = page.imageLabel.isEmpty() ? page.imageName : page.imageLabel;
    const QString stem = archiveNameFragment(label);
    return stem.isEmpty() ? QStringLiteral("folio") : stem;
}

/// The ground truth of each line of a training layout, in order.
///
/// Read off the ALTO rather than worked out from the folio, on both sides of the
/// comparison, so that what is compared is what was written and what would be
/// written — and not two different ideas of how to assemble a line.
QStringList lineTextsIn(const QByteArray &alto)
{
    QStringList lines;
    QXmlStreamReader xml(alto);
    QString current;
    bool inside = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("TextLine")) {
                inside = true;
                current.clear();
            } else if (inside && xml.name() == QLatin1String("String")) {
                current += xml.attributes().value(QLatin1String("CONTENT")).toString();
            } else if (inside && xml.name() == QLatin1String("SP")) {
                current += QLatin1Char(' ');
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("TextLine")) {
            lines.append(current);
            inside = false;
        }
    }
    return lines;
}

QByteArray contentsOfFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

QString slugFor(const TranscriptionMetadata &metadata)
{
    QString name = archiveNameFragment(metadata.shelfmark);
    if (name.isEmpty()) {
        name = archiveNameFragment(metadata.manuscriptName);
    }
    return name.isEmpty() ? QStringLiteral("unnamed") : name;
}

QString labelFor(const TranscriptionMetadata &metadata)
{
    if (!metadata.shelfmark.isEmpty()) {
        return metadata.shelfmark;
    }
    if (!metadata.manuscriptName.isEmpty()) {
        return metadata.manuscriptName;
    }
    return QStringLiteral("Unnamed manuscript");
}

QString directoryOf(const QString &slug)
{
    const QString path = QDir(root()).filePath(slug);
    QDir().mkpath(path);
    return path;
}

Set contentsOf(const QString &slug)
{
    Set set;
    set.slug = slug;
    set.label = slug;

    const QDir folder(QDir(root()).filePath(slug));
    if (!folder.exists()) {
        return set;
    }

    QFile about(folder.filePath(QLatin1String(kAbout)));
    if (about.open(QIODevice::ReadOnly)) {
        const QJsonObject json = QJsonDocument::fromJson(about.readAll()).object();
        const QString label = json.value(QStringLiteral("label")).toString();
        if (!label.isEmpty()) {
            set.label = label;
        }
    }

    for (const QFileInfo &entry :
         folder.entryInfoList({QStringLiteral("*.xml")}, QDir::Files)) {
        const int lines = linesIn(entry.absoluteFilePath());
        if (lines == 0) {
            continue;
        }
        ++set.folios;
        set.lines += lines;
        set.bytes += entry.size();
        // The picture beside it, which is the bulk of what a set weighs.
        const QFileInfo image(
            folder.filePath(entry.completeBaseName() + QStringLiteral(".jpg")));
        if (image.exists()) {
            set.bytes += image.size();
        }
    }
    return set;
}

QList<Set> known()
{
    QList<Set> sets;
    const QDir folder(root());
    if (!folder.exists()) {
        return sets;
    }
    for (const QString &slug :
         folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const Set set = contentsOf(slug);
        // A folder with nothing usable in it is not a set. It is what is left
        // after somebody emptied one, and offering it to be trained on would be
        // offering an hour of processor time for no lines.
        if (set.lines > 0) {
            sets.append(set);
        }
    }
    return sets;
}


Saved savedFolio(const TranscribedPage &page, const TranscriptionMetadata &metadata)
{
    Saved saved;
    const QDir folder(QDir(root()).filePath(slugFor(metadata)));
    const QByteArray held =
        contentsOfFile(folder.filePath(stemFor(page) + QStringLiteral(".xml")));
    if (held.isEmpty()) {
        return saved;
    }

    const QStringList before = lineTextsIn(held);
    saved.present = !before.isEmpty();
    saved.lines = int(before.size());
    if (!saved.present) {
        return saved;
    }

    // What would be written now. The size is nominal: it scales the coordinates
    // and this compares nothing but the text, so it need only be valid. The
    // folio's own picture is not fetched for a question that does not depend on
    // it — that fetch can cross the network, and this runs whenever the folio
    // changes.
    const TrainingPage truth =
        trainingAlto(page, QStringLiteral("folio.jpg"), QSize(1000, 1000));
    saved.stale = lineTextsIn(truth.alto) != before;
    return saved;
}

int add(
    const TranscribedPage &page,
    const TranscriptionMetadata &metadata,
    const QByteArray &image,
    const QSize &boxSize)
{
    const QSize size = sizeOfImage(image);
    if (!size.isValid()) {
        return 0;
    }

    // Named for the folio, so saving the same one twice replaces it. A folio
    // corrected further is a better statement of the same lines, not a second
    // one — and a model shown the same line twice, once wrong, learns the wrong
    // one as readily.
    const QString stem = stemFor(page);

    const QString imageName = stem + QStringLiteral(".jpg");
    const TrainingPage truth = trainingAlto(page, imageName, size, boxSize);
    if (truth.isEmpty()) {
        return 0;
    }

    const QDir folder(directoryOf(slugFor(metadata)));
    if (!write(folder.filePath(imageName), image)
        || !write(folder.filePath(stem + QStringLiteral(".xml")), truth.alto)) {
        return 0;
    }

    write(
        folder.filePath(QLatin1String(kAbout)),
        QJsonDocument(QJsonObject{{QStringLiteral("label"), labelFor(metadata)}})
            .toJson(QJsonDocument::Compact));

    return truth.lines;
}

} // namespace TrainingSet
} // namespace milah
