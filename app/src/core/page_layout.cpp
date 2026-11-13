#include "core/page_layout.h"

#include <QByteArray>
#include <QPoint>
#include <QRegularExpression>
#include <QStringView>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <algorithm>
#include <limits>

namespace milah {
namespace {

/// The same refusal osis.cpp makes, for the same reason. This file arrived from
/// outside Milah, and an external entity is a way to make a parser read
/// something that was never handed to it.
const QRegularExpression &forbiddenDeclarations()
{
    static const QRegularExpression expression(
        QStringLiteral("<!\\s*(?:DOCTYPE|ENTITY)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// An attribute by exact name, then by case-insensitive name.
///
/// ALTO spells its geometry HPOS and PAGE spells its imageWidth, and both are
/// written by half a dozen tools that have each been known to differ on the
/// capitals. The name is looked for as specified first, so a document that gets
/// it right costs nothing.
QStringView attribute(const QXmlStreamAttributes &attributes, QLatin1String name)
{
    for (const QXmlStreamAttribute &candidate : attributes) {
        if (candidate.name() == name) {
            return candidate.value();
        }
    }
    for (const QXmlStreamAttribute &candidate : attributes) {
        if (candidate.name().compare(name, Qt::CaseInsensitive) == 0) {
            return candidate.value();
        }
    }
    return QStringView();
}

/// A coordinate, rounded. ALTO's numbers are integers in principle and decimals
/// in practice, since a segmenter works in floating point and not every writer
/// rounds before printing.
int coordinate(QStringView text, bool *ok)
{
    bool parsed = false;
    const double value = text.toDouble(&parsed);
    if (ok) {
        *ok = parsed;
    }
    return parsed ? qRound(value) : 0;
}

QRect altoBox(const QXmlStreamAttributes &attributes)
{
    bool haveLeft = false;
    bool haveTop = false;
    bool haveWidth = false;
    bool haveHeight = false;
    const int left = coordinate(attribute(attributes, QLatin1String("HPOS")), &haveLeft);
    const int top = coordinate(attribute(attributes, QLatin1String("VPOS")), &haveTop);
    const int width = coordinate(attribute(attributes, QLatin1String("WIDTH")), &haveWidth);
    const int height = coordinate(attribute(attributes, QLatin1String("HEIGHT")), &haveHeight);

    // A word whose geometry is incomplete keeps its text and loses its box. The
    // overlay simply does not draw it, which is better than drawing it at the
    // origin — the transcription is the point, and the picture is the check.
    if (!haveLeft || !haveTop || !haveWidth || !haveHeight) {
        return QRect();
    }
    return QRect(left, top, width, height);
}

/// The bounding rectangle of a PAGE `<Coords points="x,y x,y …">` polygon.
///
/// The points of a BASELINE or a Polygon, in the order they were written.
///
/// Two spellings are in the wild and Milah reads both: ALTO's own `x1,y1 x2,y2`
/// and the bare `x1 y1 x2 y2` kraken writes. Splitting on either separator and
/// pairing what comes out settles it without asking which file this is.
QList<QPoint> parsePoints(QStringView text)
{
    QList<int> numbers;
    for (const QStringView piece : text.split(u' ', Qt::SkipEmptyParts)) {
        for (const QStringView value : piece.split(u',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const int number = coordinate(value, &ok);
            if (ok) {
                numbers.append(number);
            }
        }
    }

    QList<QPoint> points;
    // An odd trailing number is a truncated pair and is dropped rather than
    // paired with whatever follows it.
    for (int index = 0; index + 1 < numbers.size(); index += 2) {
        points.append(QPoint(numbers.at(index), numbers.at(index + 1)));
    }
    return points;
}

/// PAGE describes a word as an outline rather than a rectangle, because a word
/// on a slanted line is not a rectangle. Milah draws rectangles, so the outline
/// is reduced here, once, rather than everywhere it is used.
QRect polygonBounds(QStringView points)
{
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();
    bool any = false;

    for (const QStringView pair : points.split(u' ', Qt::SkipEmptyParts)) {
        const qsizetype comma = pair.indexOf(u',');
        if (comma < 0) {
            continue;
        }
        bool haveX = false;
        bool haveY = false;
        const int x = coordinate(pair.left(comma), &haveX);
        const int y = coordinate(pair.mid(comma + 1), &haveY);
        if (!haveX || !haveY) {
            continue;
        }
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
        any = true;
    }

    if (!any) {
        return QRect();
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

/// ALTO. Words are `<String CONTENT= HPOS= VPOS= WIDTH= HEIGHT=>` inside
/// `<TextLine>`, which is what kraken's own template writes.
RecognisedPage parseAlto(QXmlStreamReader &reader, QString *error)
{
    RecognisedPage page;
    int line = -1;
    bool insideString = false;
    bool insideLine = false;
    bool sizeSeen = false;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();

            if (name == QLatin1String("MeasurementUnit")) {
                const QString unit = reader.readElementText().trimmed();
                // mm10 and inch1200 are both legal ALTO and both describe the
                // page in units of the paper rather than of the file. Milah has
                // no scan resolution to convert them with, so a box in tenths
                // of a millimetre would land at roughly a twentieth of the way
                // across the folio and look like a segmentation fault of the
                // eye rather than of the software.
                if (!unit.isEmpty() && unit.compare(QLatin1String("pixel"), Qt::CaseInsensitive) != 0) {
                    fail(error,
                         QStringLiteral("This ALTO file measures the page in %1 rather "
                                        "than in pixels, which Milah cannot place on "
                                        "the image.")
                             .arg(unit));
                    return RecognisedPage();
                }
                continue;
            }

            if (name == QLatin1String("Page") && !sizeSeen) {
                const QXmlStreamAttributes attributes = reader.attributes();
                const int width = coordinate(attribute(attributes, QLatin1String("WIDTH")), nullptr);
                const int height = coordinate(attribute(attributes, QLatin1String("HEIGHT")), nullptr);
                if (width > 0 && height > 0) {
                    page.imageSize = QSize(width, height);
                    sizeSeen = true;
                }
                continue;
            }

            if (name == QLatin1String("TextLine")) {
                ++line;
                insideLine = true;
                // What the segmenter drew, which is what a recogniser cuts its
                // training strips from. See RecognisedLine.
                RecognisedLine found;
                found.index = line;
                found.baseline =
                    parsePoints(attribute(reader.attributes(), QLatin1String("BASELINE")));
                page.lines.append(found);
                continue;
            }

            if (name == QLatin1String("String")) {
                insideString = true;
            }

            // The line's own outline, not a word's. Kraken gives a String no
            // Shape, but a writer that does would otherwise overwrite the line
            // with the last word of it.
            if (name == QLatin1String("Polygon") && insideLine && !insideString
                && !page.lines.isEmpty()) {
                page.lines.last().boundary =
                    parsePoints(attribute(reader.attributes(), QLatin1String("POINTS")));
            }

            if (name == QLatin1String("String")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                const QString content =
                    attribute(attributes, QLatin1String("CONTENT")).toString().trimmed();
                if (content.isEmpty()) {
                    continue;
                }
                RecognisedWord word;
                word.text = content;
                word.box = altoBox(attributes);
                word.line = std::max(0, insideLine ? line : 0);
                page.words.append(word);
            }
            continue;
        }

        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("TextLine")) {
                insideLine = false;
            }
            if (reader.name() == QLatin1String("String")) {
                insideString = false;
            }
        }
    }

    return page;
}

/// PAGE. A word is a `<Word>` carrying its own `<Coords>` and its own
/// `<TextEquiv><Unicode>`; a line carries the same three things for the whole
/// line, which is why the two have to be told apart by depth rather than by
/// element name.
RecognisedPage parsePage(QXmlStreamReader &reader, QString *error)
{
    RecognisedPage page;
    int line = -1;
    bool insideLine = false;
    bool insideWord = false;
    /// Whether any line said something that a word did not. What turns a
    /// line-segmented file into a refusal rather than a blank result.
    bool lineTextSeen = false;
    bool wordSeen = false;
    RecognisedWord pending;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();

            if (name == QLatin1String("Page")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                const int width =
                    coordinate(attribute(attributes, QLatin1String("imageWidth")), nullptr);
                const int height =
                    coordinate(attribute(attributes, QLatin1String("imageHeight")), nullptr);
                if (width > 0 && height > 0) {
                    page.imageSize = QSize(width, height);
                }
                continue;
            }

            if (name == QLatin1String("TextLine")) {
                ++line;
                insideLine = true;
                // PAGE spells the same two things as child elements rather than
                // as attributes -- <Baseline points=> and <Coords points=> --
                // so the line is opened here and filled in below.
                RecognisedLine found;
                found.index = line;
                page.lines.append(found);
                continue;
            }

            // A line's own, not a word's: a Word carries Coords too, and taking
            // it would leave the line outlined round its last word.
            if (insideLine && !insideWord && !page.lines.isEmpty()) {
                if (name == QLatin1String("Baseline")) {
                    page.lines.last().baseline =
                        parsePoints(attribute(reader.attributes(), QLatin1String("points")));
                } else if (name == QLatin1String("Coords")) {
                    page.lines.last().boundary =
                        parsePoints(attribute(reader.attributes(), QLatin1String("points")));
                }
            }

            if (name == QLatin1String("Word")) {
                insideWord = true;
                pending = RecognisedWord();
                pending.line = std::max(0, line);
                continue;
            }

            if (name == QLatin1String("Coords") && insideWord) {
                pending.box = polygonBounds(attribute(reader.attributes(),
                                                      QLatin1String("points")));
                continue;
            }

            if (name == QLatin1String("Unicode")) {
                const QString text = reader.readElementText().trimmed();
                if (insideWord) {
                    pending.text = text;
                } else if (insideLine && !text.isEmpty()) {
                    lineTextSeen = true;
                }
                continue;
            }
            continue;
        }

        if (token != QXmlStreamReader::EndElement) {
            continue;
        }

        const QStringView name = reader.name();
        if (name == QLatin1String("Word")) {
            insideWord = false;
            if (!pending.text.isEmpty()) {
                page.words.append(pending);
                wordSeen = true;
            }
            continue;
        }
        if (name == QLatin1String("TextLine")) {
            insideLine = false;
        }
    }

    // Kraken and eScriptorium can both be asked for line-level PAGE, and it is
    // the more common export of the two. Saying so is the point: the file is
    // perfectly good, it simply does not carry what the overlay is made of, and
    // a transcriber told that can go and re-export it. Reading the lines in as
    // though they were words would put a sentence in every cell of the grid.
    if (!wordSeen && lineTextSeen) {
        fail(error,
             QStringLiteral("This PAGE file is segmented into lines but not into "
                            "words, so Milah cannot place the text on the image. "
                            "Export it again with word-level segmentation."));
        return RecognisedPage();
    }

    return page;
}

} // namespace

RecognisedPage parseRecognisedPage(const QByteArray &xml, QString *error)
{
    if (error) {
        error->clear();
    }

    if (forbiddenDeclarations().match(QString::fromUtf8(xml)).hasMatch()) {
        fail(error,
             QStringLiteral("Layout files containing DTD or ENTITY declarations "
                            "are not allowed."));
        return RecognisedPage();
    }

    QXmlStreamReader reader(xml);

    // The root decides everything, so it is read on its own before either
    // reader starts: both walk the whole document, and neither would recognise
    // being handed the other's.
    QString root;
    while (!reader.atEnd() && root.isEmpty()) {
        if (reader.readNext() == QXmlStreamReader::StartElement) {
            root = reader.name().toString();
        }
    }

    RecognisedPage page;
    if (root.compare(QLatin1String("alto"), Qt::CaseInsensitive) == 0) {
        page = parseAlto(reader, error);
    } else if (root.compare(QLatin1String("PcGts"), Qt::CaseInsensitive) == 0) {
        page = parsePage(reader, error);
    } else if (reader.hasError()) {
        // Falls through to the report below, which says where.
    } else {
        fail(error,
             root.isEmpty()
                 ? QStringLiteral("This file is empty.")
                 : QStringLiteral("This is not an ALTO or PAGE layout file: it "
                                  "begins with <%1>.")
                       .arg(root));
        return RecognisedPage();
    }

    if (error && !error->isEmpty()) {
        return RecognisedPage();
    }

    if (reader.hasError()) {
        fail(error,
             QStringLiteral("%1 (line %2, column %3)")
                 .arg(reader.errorString())
                 .arg(reader.lineNumber())
                 .arg(reader.columnNumber()));
        return RecognisedPage();
    }

    // Without this the overlay has no scale factor, and every box would be
    // drawn as though the folio were exactly the size the recogniser saw.
    // Sometimes that is even true, which is what makes it worth refusing.
    if (!page.words.isEmpty() && !page.imageSize.isValid()) {
        fail(error,
             QStringLiteral("This layout file does not say how large the page "
                            "was, so Milah cannot place the words on it."));
        return RecognisedPage();
    }

    return page;
}

} // namespace milah
