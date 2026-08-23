#include "core/training_export.h"

#include "core/transcription.h"

#include <QMap>
#include <QRect>
#include <QStringList>
#include <QXmlStreamWriter>

#include <algorithm>
#include <functional>

namespace milah {
namespace {

/// The words of one line, in the order they were read.
using Line = QList<const TranscribedWord *>;

/// Kraken's own floor is a 5px baseline. Kept a little above it: a line that
/// narrow is one short word and teaches a model nothing worth the row.
constexpr int MinimumLineWidth = 8;

/// `box` brought inside a picture `size` pixels across.
///
/// Kraken tests a polygon against the picture with `>=`, so a rectangle whose
/// right edge lands on the width is *out* of bounds — and scaling a box up from
/// the screen-sized picture to the master rounds both the origin and the width,
/// either of which can push it there. The line is then dropped with a log line
/// nobody reads.
QRect inside(const QRect &box, const QSize &size)
{
    const int left = std::clamp(box.left(), 0, std::max(0, size.width() - 1));
    const int top = std::clamp(box.top(), 0, std::max(0, size.height() - 1));
    const int right = std::clamp(box.right(), left, std::max(0, size.width() - 1));
    const int bottom = std::clamp(box.bottom(), top, std::max(0, size.height() - 1));
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

/// Whether this word can be taught: somebody has read it, and it has a place on
/// the picture.
bool isVouchedFor(const TranscribedWord *word)
{
    return !word->unchecked && !word->box.isNull() && !word->hebrew.isEmpty();
}

/// The line as much of it as can be taught, or empty when none of it can.
///
/// A marginal word nobody has read yet is ink in the picture with no truth to go
/// with it. Where such words sit at the **ends** of the line, dropping them
/// leaves a shorter line that is wholly true — kraken masks everything outside
/// the polygon to zero, so a polygon drawn round what is left really does keep
/// that ink out of what the model sees. Where one sits in the **middle**, there
/// is no polygon round the rest of the line that excludes it, so nothing here
/// can be taught.
///
/// Everything else is unchanged: a word that is unchecked, unboxed or unread is
/// still a refusal for the whole line, marginal or not.
Line groundTruth(const Line &line)
{
    int first = 0;
    int last = int(line.size()) - 1;
    while (first <= last && line.at(first)->marginal && !isVouchedFor(line.at(first))) {
        ++first;
    }
    while (last >= first && line.at(last)->marginal && !isVouchedFor(line.at(last))) {
        --last;
    }
    if (first > last) {
        // All of it was margin nobody has read.
        return Line();
    }

    Line kept;
    for (int index = first; index <= last; ++index) {
        if (!isVouchedFor(line.at(index))) {
            // Inside the line now, so it cannot be trimmed away — including a
            // marginal note the recogniser folded into the middle of a text
            // line, which is the case this whole shape exists for.
            return Line();
        }
        kept.append(line.at(index));
    }
    return kept;
}

QRect boundsOf(const Line &line)
{
    QRect bounds;
    for (const TranscribedWord *word : line) {
        bounds = bounds.isNull() ? word->box : bounds.united(word->box);
    }
    return bounds;
}

/// The lines of a folio, in the order they were read.
///
/// **A line ends where the recogniser said it ends, or where the transcriber
/// said it does.** The recogniser's own idea of a line is often wrong on a hand
/// it was not trained for, so `endsLine` is how a person says otherwise — and
/// because it only ever adds a break, every line here is a run of words that
/// shared a recognised line, which is what keeps its box honest.
QList<Line> linesOf(const TranscribedPage &page)
{
    QList<Line> lines;
    int previous = -1;
    bool broken = true;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            // A word nothing read has no line, and a verse boundary is Milah's
            // idea rather than the folio's — the lines cut across the verses,
            // which is exactly why they are recorded per word.
            if (word.line < 0) {
                continue;
            }
            if (broken || word.line != previous) {
                lines.append(Line());
            }
            lines.last().append(&word);
            previous = word.line;
            broken = word.endsLine;
        }
    }
    return lines;
}

void writeBox(QXmlStreamWriter &xml, const QRect &box)
{
    xml.writeAttribute(QStringLiteral("HPOS"), QString::number(box.x()));
    xml.writeAttribute(QStringLiteral("VPOS"), QString::number(box.y()));
    xml.writeAttribute(QStringLiteral("WIDTH"), QString::number(box.width()));
    xml.writeAttribute(QStringLiteral("HEIGHT"), QString::number(box.height()));
}

/// The points of a line the recogniser drew, scaled and brought inside the
/// picture, and clipped to `clip`.
///
/// The clip is what lets one detection be exported as more than one line. Two
/// cases need it, and they pull in different directions:
///
/// - an unread marginal note trimmed off the **end** of a line, where the shape
///   should still follow the ink and merely stop short;
/// - a detection the transcriber has cut in two with `endsLine`, because the
///   segmenter ran two manuscript lines together. Here the two halves are
///   **stacked**, so a clip in x alone would hand both of them the same polygon
///   and teach the model a strip with two lines of ink in it.
///
/// So both axes are clipped. Points outside are pulled to the edge rather than
/// dropped, so a boundary stays a closed ring and a baseline keeps its ends.
/// `clip` is expected to have been grown vertically by the caller: a segmenter's
/// outline reaches above the word boxes on purpose, round the ascenders, and
/// squeezing it to their band would shave the very ink the strip is cut for.
QString pointsInside(
    const QList<QPoint> &points,
    const std::function<QRect(const QRect &)> &scaled,
    const QSize &size,
    const QRect &clip)
{
    QStringList parts;
    parts.reserve(points.size() * 2);
    for (const QPoint &point : points) {
        // Through the same rectangle scaling the boxes go through, so a point
        // and a box that touched on the screen still touch on the master.
        const QRect at = inside(scaled(QRect(point, QSize(1, 1))), size);
        parts << QString::number(std::clamp(at.x(), clip.left(), clip.right()))
              << QString::number(std::clamp(at.y(), clip.top(), clip.bottom()));
    }
    return parts.join(QLatin1Char(' '));
}

/// The line a baseline model reads when the folio has no real one: level,
/// through the middle of the words.
///
/// A fallback now rather than the rule. Milah keeps what the segmenter drew
/// (see TranscribedLine), but a folio read before it did, or an imported file
/// that records no baseline, still has to produce something a baseline model
/// will take — and a level line through the middle of the boxes is a fair
/// statement of a manuscript line that runs straight.
QString baselineOf(const QRect &bounds)
{
    const int middle = bounds.y() + bounds.height() / 2;
    return QStringLiteral("%1 %2 %3 %4")
        .arg(bounds.left())
        .arg(middle)
        .arg(bounds.right())
        .arg(middle);
}

/// The line's outline, as the rectangle round its words.
QString polygonOf(const QRect &bounds)
{
    return QStringLiteral("%1 %2 %3 %2 %3 %4 %1 %4")
        .arg(bounds.left())
        .arg(bounds.top())
        .arg(bounds.right())
        .arg(bounds.bottom());
}

} // namespace

TrainingPage trainingAlto(
    const TranscribedPage &page,
    const QString &imageName,
    const QSize &imageSize,
    const QSize &boxSize)
{
    TrainingPage result;
    if (!imageSize.isValid() || imageName.isEmpty()) {
        return result;
    }

    // From the picture the boxes were stored against to the one being written.
    // Ground truth is cut from the library's largest scan while the boxes are in
    // the coordinates of the smaller picture on screen, so every rectangle has
    // to be carried across — and a line cut at the wrong place teaches the model
    // whatever happens to be there.
    const QSize from = boxSize.isValid() ? boxSize : imageSize;
    const double scaleX = double(imageSize.width()) / from.width();
    const double scaleY = double(imageSize.height()) / from.height();
    const auto scaled = [scaleX, scaleY](const QRect &box) {
        return QRect(
            qRound(box.x() * scaleX),
            qRound(box.y() * scaleY),
            qRound(box.width() * scaleX),
            qRound(box.height() * scaleY));
    };

    // Keyed by the line number a word carries, which is how a Line finds the
    // geometry that belongs to it.
    QMap<int, const TranscribedLine *> drawnLines;
    for (const TranscribedLine &drawn : page.lines) {
        drawnLines.insert(drawn.index, &drawn);
    }

    const QList<Line> lines = linesOf(page);
    result.candidates = int(lines.size());

    // How many boxed words each detection holds, so an exported line can tell
    // whether it *is* that detection or only a part of one. Both an unread
    // marginal note trimmed off an end and a break the transcriber put in with
    // endsLine leave a group smaller than the detection it came from, and both
    // mean the segmenter's shape is no longer the shape of what is being taught.
    QMap<int, int> detectionSize;
    for (const TranscribedVerse &verse : page.verses) {
        for (const TranscribedWord &word : verse.words) {
            if (word.line >= 0 && !word.box.isNull()) {
                detectionSize[word.line] += 1;
            }
        }
    }

    QByteArray document;
    QXmlStreamWriter xml(&document);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("alto"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.loc.gov/standards/alto/ns-v4#"));

    xml.writeStartElement(QStringLiteral("Description"));
    // Pixels, and said out loud. ALTO may also measure a page in tenths of a
    // millimetre, and a reader that assumed one or the other would put every
    // box in the wrong place on half the files in the world.
    xml.writeTextElement(QStringLiteral("MeasurementUnit"), QStringLiteral("pixel"));
    xml.writeStartElement(QStringLiteral("sourceImageInformation"));
    // The file beside this one. Kraken resolves it against the XML's own
    // directory, so a folio's label — "150r" — names nothing and takes the
    // whole page down with it.
    xml.writeTextElement(QStringLiteral("fileName"), imageName);
    xml.writeEndElement();
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Layout"));
    xml.writeStartElement(QStringLiteral("Page"));
    xml.writeAttribute(QStringLiteral("WIDTH"), QString::number(imageSize.width()));
    xml.writeAttribute(QStringLiteral("HEIGHT"), QString::number(imageSize.height()));
    xml.writeAttribute(QStringLiteral("PHYSICAL_IMG_NR"), QStringLiteral("0"));
    xml.writeStartElement(QStringLiteral("PrintSpace"));
    xml.writeAttribute(QStringLiteral("HPOS"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("VPOS"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("WIDTH"), QString::number(imageSize.width()));
    xml.writeAttribute(QStringLiteral("HEIGHT"), QString::number(imageSize.height()));
    xml.writeStartElement(QStringLiteral("TextBlock"));
    xml.writeAttribute(QStringLiteral("ID"), QStringLiteral("block_0"));

    for (int index = 0; index < lines.size(); ++index) {
        const Line line = groundTruth(lines.at(index));
        if (line.isEmpty()) {
            continue;
        }

        const QRect bounds = inside(scaled(boundsOf(line)), imageSize);
        // Kraken refuses a baseline under 5px and then carries on without
        // saying so, one line at a time. A line this thin teaches nothing
        // anyway, and leaving it out is the difference between a training set
        // that is smaller than the transcriber thinks and one that is not.
        if (bounds.width() < MinimumLineWidth || bounds.height() < 1) {
            continue;
        }
        // What the segmenter actually drew, where the folio has it. Kraken
        // dewarps the strip along the baseline and masks it to the boundary, so
        // these decide what the model is shown — and Milah's own level line and
        // rectangle are a poor stand-in for a sloping line on a crowded leaf.
        // Clipped rather than replaced where a marginal word was trimmed off an
        // end, so the shape still follows the ink and merely stops short.
        const TranscribedLine *drawn = drawnLines.value(line.first()->line, nullptr);
        // Clipped only where this line is not the whole of the detection it came
        // from — a marginal note trimmed off an end, or a break the transcriber
        // put in with endsLine because the segmenter ran two manuscript lines
        // together. A line nothing was taken from keeps the shape whole.
        //
        // Grown by half its own height before it clips, because the segmenter's
        // outline reaches past the word boxes on purpose, round the ascenders
        // and under the descenders, and squeezing it to their band would shave
        // the very ink the strip is cut for. Two stacked halves therefore still
        // overlap a little; they are no longer the same shape, which is the
        // whole of the difference between one usable strip and two useless ones.
        const bool whole = line.size() == detectionSize.value(line.first()->line);
        const QRect clip =
            whole ? QRect(QPoint(0, 0), imageSize)
                  : inside(
                        bounds.adjusted(0, -bounds.height() / 2, 0, bounds.height() / 2),
                        imageSize);
        const QString baseline =
            drawn && drawn->baseline.size() >= 2
                ? pointsInside(drawn->baseline, scaled, imageSize, clip)
                : baselineOf(bounds);
        const QString outline =
            drawn && drawn->boundary.size() >= 3
                ? pointsInside(drawn->boundary, scaled, imageSize, clip)
                : polygonOf(bounds);

        xml.writeStartElement(QStringLiteral("TextLine"));
        xml.writeAttribute(QStringLiteral("ID"), QStringLiteral("line_%1").arg(index));
        xml.writeAttribute(QStringLiteral("BASELINE"), baseline);
        writeBox(xml, bounds);
        xml.writeStartElement(QStringLiteral("Shape"));
        xml.writeStartElement(QStringLiteral("Polygon"));
        xml.writeAttribute(QStringLiteral("POINTS"), outline);
        xml.writeEndElement();
        xml.writeEndElement();

        bool first = true;
        for (const TranscribedWord *word : line) {
            // Between the words and nowhere else. The line's text is the
            // CONTENTs and the SPs joined, so a missing one runs two words
            // together and a leading one puts a space where the line begins.
            if (!first) {
                xml.writeEmptyElement(QStringLiteral("SP"));
            }
            first = false;
            xml.writeStartElement(QStringLiteral("String"));
            xml.writeAttribute(QStringLiteral("CONTENT"), word->hebrew);
            writeBox(xml, inside(scaled(word->box), imageSize));
            xml.writeEndElement();
        }
        xml.writeEndElement();

        ++result.lines;
        result.words += int(line.size());
    }

    xml.writeEndElement(); // TextBlock
    xml.writeEndElement(); // PrintSpace
    xml.writeEndElement(); // Page
    xml.writeEndElement(); // Layout
    xml.writeEndElement(); // alto
    xml.writeEndDocument();

    // An empty file is worse than none: it looks like ground truth, trains on
    // nothing, and says nothing about why.
    if (result.lines == 0) {
        // The count of what there is to read stands even so — that is what a
        // folio nobody has been through yet has to be able to say.
        TrainingPage none;
        none.candidates = result.candidates;
        return none;
    }
    result.alto = document;
    return result;
}

} // namespace milah
