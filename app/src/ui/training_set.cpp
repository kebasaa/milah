#include "ui/training_set.h"

#include "core/training_export.h"
#include "core/transcription.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>
#include <QStandardPaths>

#include <algorithm>

namespace milah {
namespace TrainingSet {
namespace {

/// What a set is called on screen, kept beside its files.
///
/// The slug cannot say it: "MSOo132" is a folder name, and the transcriber
/// wrote "MS Oo.1.32". One small file rather than a settings key, so a set that
/// is copied to another machine arrives knowing its own name.
///
/// It also keeps each folio's label under its stem, for the same reason: the
/// stem is a file name, and "150r" is what the transcriber knows the leaf by.
constexpr char kAbout[] = "about.json";

QString root()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("htr-training"));
}

/// Eight hex digits of a hash of `text` as written.
///
/// Enough to tell apart the folios of a codex, or the manuscripts a transcriber
/// will ever train, and short enough to leave the readable part of a name
/// readable.
QString shortHash(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

/// A readable fragment of `text` with a hash of all of it after, so that the
/// names stay legible and two texts that clean to the same fragment — two
/// labels in Hebrew, "f. 1r" and "f.1r" — still name two different things.
QString uniqueName(const QString &text, const QString &fallback)
{
    QString fragment = archiveNameFragment(text);
    if (fragment.isEmpty()) {
        fragment = fallback;
    }
    return fragment + QLatin1Char('-') + shortHash(text);
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

/// The picture as kraken will see it: upright.
///
/// The size the boxes are measured against is read with the file's own
/// orientation applied, and so is the picture on screen — but kraken opens the
/// file with Pillow, which applies none. A photograph a phone took sideways
/// would then be written beside a layout measured the other way round, and
/// every line cut from the wrong place. So a picture carrying an orientation is
/// written already turned, and one that carries none is written untouched.
QByteArray upright(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (reader.transformation() == QImageIOHandler::TransformationNone) {
        return bytes;
    }
    reader.setAutoTransform(true);
    const QImage turned = reader.read();
    if (turned.isNull()) {
        return bytes;
    }
    QByteArray out;
    QBuffer written(&out);
    written.open(QIODevice::WriteOnly);
    return turned.save(&written, "JPG", 92) ? out : bytes;
}

/// How many `<TextLine>`s a saved file holds.
///
/// Counted out of the file rather than remembered in a tally, so a folder
/// somebody has tidied by hand still reports what is actually in it.
int linesIn(const QByteArray &alto)
{
    return int(alto.count(QByteArray("<TextLine ")));
}

int linesIn(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    return linesIn(file.readAll());
}

bool write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.commit();
}

QJsonObject aboutIn(const QDir &folder)
{
    QFile about(folder.filePath(QLatin1String(kAbout)));
    if (!about.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(about.readAll()).object();
}

void writeAbout(const QDir &folder, const QJsonObject &about)
{
    write(folder.filePath(QLatin1String(kAbout)),
          QJsonDocument(about).toJson(QJsonDocument::Compact));
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

/// The size of the picture a saved layout was measured against, and which
/// folio it says it is.
struct Header
{
    QSize size;
    QString identity;
};

Header headerOf(const QByteArray &alto)
{
    Header header;
    QXmlStreamReader xml(alto);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() == QLatin1String("fileIdentifier")) {
            header.identity = xml.readElementText().trimmed();
        } else if (xml.name() == QLatin1String("Page")) {
            header.size = QSize(xml.attributes().value(QLatin1String("WIDTH")).toInt(),
                                xml.attributes().value(QLatin1String("HEIGHT")).toInt());
            // Everything wanted is above the first line.
            break;
        }
    }
    return header;
}

QByteArray contentsOfFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// Deletes a folio's pair of files and its entry in the set's about file.
bool removeFiles(const QDir &folder, const QString &stem)
{
    bool removed = false;
    for (const QString &extension : {QStringLiteral(".xml"), QStringLiteral(".jpg")}) {
        removed = QFile::remove(folder.filePath(stem + extension)) || removed;
    }
    QJsonObject about = aboutIn(folder);
    QJsonObject folios = about.value(QStringLiteral("folios")).toObject();
    if (folios.contains(stem)) {
        folios.remove(stem);
        about.insert(QStringLiteral("folios"), folios);
        writeAbout(folder, about);
    }
    return removed;
}

} // namespace

QByteArray uprightImage(const QByteArray &bytes)
{
    return upright(bytes);
}

QString slugFor(const TranscriptionMetadata &metadata)
{
    if (!metadata.shelfmark.trimmed().isEmpty()) {
        return uniqueName(metadata.shelfmark, QStringLiteral("manuscript"));
    }
    if (!metadata.manuscriptName.trimmed().isEmpty()) {
        return uniqueName(metadata.manuscriptName, QStringLiteral("manuscript"));
    }
    return QStringLiteral("unnamed");
}

QString stemFor(const TranscribedPage &page)
{
    // Readable by the label, told apart by the identity — which for a scanned
    // folio is its address, and so differs where two labels do not.
    const QString label = page.imageLabel.isEmpty() ? page.imageName : page.imageLabel;
    QString fragment = archiveNameFragment(label);
    if (fragment.isEmpty()) {
        fragment = QStringLiteral("folio");
    }
    return fragment + QLatin1Char('-') + shortHash(folioIdentity(page));
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

    const QString label = aboutIn(folder).value(QStringLiteral("label")).toString();
    if (!label.isEmpty()) {
        set.label = label;
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

QList<Folio> foliosOf(const QString &slug)
{
    QList<Folio> folios;
    const QDir folder(QDir(root()).filePath(slug));
    if (!folder.exists()) {
        return folios;
    }
    const QJsonObject labels =
        aboutIn(folder).value(QStringLiteral("folios")).toObject();
    for (const QFileInfo &entry :
         folder.entryInfoList({QStringLiteral("*.xml")}, QDir::Files)) {
        const QByteArray alto = contentsOfFile(entry.absoluteFilePath());
        Folio folio;
        folio.stem = entry.completeBaseName();
        folio.label = labels.value(folio.stem).toString();
        if (folio.label.isEmpty()) {
            folio.label = folio.stem;
        }
        folio.identity = headerOf(alto).identity;
        folio.lines = linesIn(alto);
        folio.saved = entry.lastModified();
        folios.append(folio);
    }
    std::sort(folios.begin(), folios.end(), [](const Folio &a, const Folio &b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    return folios;
}

QString lettersOf(const QString &slug)
{
    QSet<QChar> seen;
    const QDir folder(QDir(root()).filePath(slug));
    for (const QFileInfo &entry :
         folder.entryInfoList({QStringLiteral("*.xml")}, QDir::Files)) {
        for (const QString &line : lineTextsIn(contentsOfFile(entry.absoluteFilePath()))) {
            for (const QChar letter : line) {
                if (!letter.isSpace()) {
                    seen.insert(letter);
                }
            }
        }
    }
    QList<QChar> sorted(seen.cbegin(), seen.cend());
    std::sort(sorted.begin(), sorted.end());
    return QString(sorted.constData(), sorted.size());
}

QByteArray altoOf(const QString &slug, const QString &stem)
{
    return contentsOfFile(
        QDir(QDir(root()).filePath(slug)).filePath(stem + QStringLiteral(".xml")));
}

bool remove(const QString &slug, const QString &stem)
{
    if (stem.isEmpty() || stem.contains(QLatin1Char('/')) || stem.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QDir folder(QDir(root()).filePath(slug));
    return folder.exists() && removeFiles(folder, stem);
}

Saved savedFolio(
    const TranscribedPage &page,
    const TranscriptionMetadata &metadata,
    const QSize &boxSize)
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

    // What would be written now, at the size it was written at. The folio's
    // own picture is not fetched for a question that does not depend on its
    // pixels — that fetch can cross the network, and this runs whenever the
    // folio changes — but the size is in the saved file, and it decides which
    // lines are clamped away.
    const QSize size = headerOf(held).size;
    const TrainingPage truth = trainingAlto(
        page, QStringLiteral("folio.jpg"), size.isValid() ? size : QSize(1000, 1000), boxSize);
    saved.stale = lineTextsIn(truth.alto) != before;
    return saved;
}

bool withdraw(const TranscribedPage &page, const TranscriptionMetadata &metadata)
{
    return remove(slugFor(metadata), stemFor(page));
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
        // Nothing of it is ground truth any more, so nothing of it may stay in
        // the set to be taught.
        withdraw(page, metadata);
        return 0;
    }

    const QDir folder(directoryOf(slugFor(metadata)));
    if (!write(folder.filePath(imageName), upright(image))
        || !write(folder.filePath(stem + QStringLiteral(".xml")), truth.alto)) {
        return 0;
    }

    QJsonObject about = aboutIn(folder);
    about.insert(QStringLiteral("label"), labelFor(metadata));
    QJsonObject folios = about.value(QStringLiteral("folios")).toObject();
    folios.insert(stem, page.imageLabel.isEmpty() ? page.imageName : page.imageLabel);
    about.insert(QStringLiteral("folios"), folios);
    writeAbout(folder, about);

    return truth.lines;
}

} // namespace TrainingSet
} // namespace milah
