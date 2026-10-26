#include "core/training_export.h"

#include "core/transcription.h"

#include <QMap>
#include <QPointF>
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

/// Why a word that is not vouched for is not.
LineVerdict::Reason reasonFor(const TranscribedWord *word)
{
    if (word->unchecked) {
        return LineVerdict::Reason::UncheckedWord;
    }
    if (word->box.isNull()) {
        return LineVerdict::Reason::WordWithoutBox;
    }
    return LineVerdict::Reason::NoReading;
}

/// Where a word sits in the folio's verses.
struct Place
{
    int verse = -1;
    int word = -1;
};

/// The words that shared one recognised line, between the breaks a transcriber
/// put into it — one candidate for a line of ground truth.
struct Group
{
    Line words;
    /// Parallel to `words`.
    QList<Place> places;
    int detection = -1;
    int part = 0;
    /// A typed word standing beside it, which nothing places on the picture.
    /// Invalid where there is none.
    Place loose;
};

/// The line as much of it as can be taught, or empty when none of it can —
/// and what the export makes of it.
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
/// still a refusal for the whole line, marginal or not — and so is a typed word
/// standing beside it, whose letters the line would otherwise be written
/// without.
Line groundTruth(const Group &group, LineVerdict *verdict)
{
    verdict->detection = group.detection;
    verdict->part = group.part;
    const auto refuse = [verdict](LineVerdict::Reason reason, const Place &place) {
        verdict->state = LineVerdict::State::Refused;
        verdict->reason = reason;
        verdict->verse = place.verse;
        verdict->word = place.word;
        return Line();
    };

    const Line &line = group.words;
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
        return refuse(reasonFor(line.constFirst()), group.places.constFirst());
    }

    Line kept;
    for (int index = first; index <= last; ++index) {
        const TranscribedWord *word = line.at(index);
        if (!isVouchedFor(word)) {
            // Inside the line now, so it cannot be trimmed away — including a
            // marginal note the recogniser folded into the middle of a text
            // line, which is the case this whole shape exists for.
            const LineVerdict::Reason reason = word->marginal && word->unchecked
                ? LineVerdict::Reason::MarginInMiddle
                : reasonFor(word);
            return refuse(reason, group.places.at(index));
        }
        kept.append(word);
    }

    // Asked after the words, because checking them is the work a transcriber
    // is in the middle of, and "2 unchecked" is the more useful thing to say
    // until it is done.
    if (group.loose.verse >= 0) {
        return refuse(LineVerdict::Reason::LooseWord, group.loose);
    }

    verdict->state = kept.size() == line.size() ? LineVerdict::State::Ready
                                                 : LineVerdict::State::Trimmed;
    verdict->reason = LineVerdict::Reason::None;
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
///
/// **A typed word is not stepped over.** It has no line, so it belongs to none
/// of these groups — but it does stand among them, and its letters are on the
/// folio somewhere near. The group before it and the group after it are both
/// marked, and where those are one group it is marked once.
QList<Group> linesOf(const TranscribedPage &page)
{
    QList<Group> lines;
    QMap<int, int> parts;
    int previous = -1;
    bool broken = true;
    // A typed word seen since the last word with a line, waiting to learn
    // which group comes after it.
    Place loose;
    for (int v = 0; v < page.verses.size(); ++v) {
        const QList<TranscribedWord> &words = page.verses.at(v).words;
        for (int w = 0; w < words.size(); ++w) {
            const TranscribedWord &word = words.at(w);
            // A verse boundary is Milah's idea rather than the folio's — the
            // lines cut across the verses, which is exactly why they are
            // recorded per word.
            if (word.line < 0) {
                // An empty cell is somewhere to type, not a word.
                if (word.hebrew.trimmed().isEmpty()) {
                    continue;
                }
                if (loose.verse < 0) {
                    loose = Place{v, w};
                }
                if (!lines.isEmpty() && lines.last().loose.verse < 0) {
                    lines.last().loose = Place{v, w};
                }
                continue;
            }
            if (broken || word.line != previous) {
                Group group;
                group.detection = word.line;
                group.part = parts[word.line]++;
                lines.append(group);
            }
            if (loose.verse >= 0) {
                if (lines.last().loose.verse < 0) {
                    lines.last().loose = loose;
                }
                loose = Place();
            }
            lines.last().words.append(&word);
            lines.last().places.append(Place{v, w});
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

/// The points as ALTO writes them, rounded to the pixel, with a point that
/// rounding has made the same as the one before it left out.
QString pointsText(const QList<QPointF> &points)
{
    QStringList parts;
    parts.reserve(points.size() * 2);
    QPoint last(-1, -1);
    for (const QPointF &point : points) {
        const QPoint at = point.toPoint();
        if (!parts.isEmpty() && at == last) {
            continue;
        }
        parts << QString::number(at.x()) << QString::number(at.y());
        last = at;
    }
    return parts.join(QLatin1Char(' '));
}

/// How many distinct pixels pointsText() would write.
int distinctPoints(const QList<QPointF> &points)
{
    int count = 0;
    QPoint last(-1, -1);
    for (const QPointF &point : points) {
        const QPoint at = point.toPoint();
        if (count == 0 || at != last) {
            ++count;
        }
        last = at;
    }
    return count;
}

/// A baseline cut to the columns of `clip`, and held inside its rows.
///
/// **Cut, not clamped.** Pulling every point outside the clip to its edge
/// stacks them there at their own heights, so a baseline that sloped past the
/// edge came back doubling down and up again at it — and kraken dewarps the
/// strip along exactly this line. Here a segment that crosses the edge is cut
/// where it crosses, so the line stops short and stays a line.
///
/// A baseline that leaves the clip and comes back — which a sane one does not —
/// keeps its longest stretch inside.
QList<QPointF> clippedBaseline(const QList<QPointF> &points, const QRect &clip)
{
    const double left = clip.left();
    const double right = clip.right();

    QList<QList<QPointF>> runs;
    QList<QPointF> run;
    const auto close = [&runs, &run] {
        if (run.size() >= 2) {
            runs.append(run);
        }
        run.clear();
    };

    for (int index = 0; index + 1 < points.size(); ++index) {
        const QPointF from = points.at(index);
        const QPointF to = points.at(index + 1);
        const double dx = to.x() - from.x();
        double enter = 0.0;
        double leave = 1.0;
        if (dx == 0.0) {
            if (from.x() < left || from.x() > right) {
                close();
                continue;
            }
        } else {
            double a = (left - from.x()) / dx;
            double b = (right - from.x()) / dx;
            if (a > b) {
                std::swap(a, b);
            }
            enter = std::max(0.0, a);
            leave = std::min(1.0, b);
            if (enter > leave) {
                close();
                continue;
            }
        }
        const QPointF in = from + (to - from) * enter;
        const QPointF out = from + (to - from) * leave;
        if (!run.isEmpty() && run.last() != in) {
            close();
        }
        if (run.isEmpty()) {
            run.append(in);
        }
        run.append(out);
        if (leave < 1.0) {
            close();
        }
    }
    close();

    QList<QPointF> longest;
    double widest = -1.0;
    for (const QList<QPointF> &candidate : runs) {
        double lowest = candidate.constFirst().x();
        double highest = lowest;
        for (const QPointF &point : candidate) {
            lowest = std::min(lowest, point.x());
            highest = std::max(highest, point.x());
        }
        if (highest - lowest > widest) {
            widest = highest - lowest;
            longest = candidate;
        }
    }
    for (QPointF &point : longest) {
        point.setY(std::clamp(point.y(), double(clip.top()), double(clip.bottom())));
    }
    return longest;
}

/// An outline cut to `clip`, as a closed ring.
///
/// One edge of the rectangle at a time (Sutherland and Hodgman), which is exact
/// for a rectangle and keeps the ring's own order. Clamping each corner into the
/// clip instead used to fold the ring over itself wherever two corners on one
/// side were pulled to the same edge at different heights.
QList<QPointF> clippedOutline(const QList<QPointF> &ring, const QRect &clip)
{
    const double left = clip.left();
    const double right = clip.right();
    const double top = clip.top();
    const double bottom = clip.bottom();

    // Each edge of the clip as a test of which side a point is on, and where a
    // segment crossing it does.
    using Inside = std::function<bool(const QPointF &)>;
    using Cross = std::function<QPointF(const QPointF &, const QPointF &)>;
    const auto atX = [](double x) {
        return [x](const QPointF &a, const QPointF &b) {
            const double t = (x - a.x()) / (b.x() - a.x());
            return QPointF(x, a.y() + (b.y() - a.y()) * t);
        };
    };
    const auto atY = [](double y) {
        return [y](const QPointF &a, const QPointF &b) {
            const double t = (y - a.y()) / (b.y() - a.y());
            return QPointF(a.x() + (b.x() - a.x()) * t, y);
        };
    };
    const QList<QPair<Inside, Cross>> edges{
        {[left](const QPointF &p) { return p.x() >= left; }, atX(left)},
        {[right](const QPointF &p) { return p.x() <= right; }, atX(right)},
        {[top](const QPointF &p) { return p.y() >= top; }, atY(top)},
        {[bottom](const QPointF &p) { return p.y() <= bottom; }, atY(bottom)},
    };

    QList<QPointF> out = ring;
    for (const auto &[isInside, crossing] : edges) {
        if (out.isEmpty()) {
            break;
        }
        const QList<QPointF> in = out;
        out.clear();
        QPointF previous = in.constLast();
        for (const QPointF &current : in) {
            if (isInside(current)) {
                if (!isInside(previous)) {
                    out.append(crossing(previous, current));
                }
                out.append(current);
            } else if (isInside(previous)) {
                out.append(crossing(previous, current));
            }
            previous = current;
        }
    }
    return out;
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

/// From the picture the boxes were stored against to the one being written.
std::function<QRect(const QRect &)> scalingFor(const QSize &imageSize, const QSize &boxSize)
{
    const QSize from = boxSize.isValid() ? boxSize : imageSize;
    const double scaleX = double(imageSize.width()) / from.width();
    const double scaleY = double(imageSize.height()) / from.height();
    return [scaleX, scaleY](const QRect &box) {
        return QRect(
            qRound(box.x() * scaleX),
            qRound(box.y() * scaleY),
            qRound(box.width() * scaleX),
            qRound(box.height() * scaleY));
    };
}

/// Kraken refuses a baseline under 5px and then carries on without saying so,
/// one line at a time. A line this thin teaches nothing anyway, and leaving it
/// out is the difference between a training set that is smaller than the
/// transcriber thinks and one that is not.
bool isTooNarrow(const QRect &bounds)
{
    return bounds.width() < MinimumLineWidth || bounds.height() < 1;
}

/// A line refused for being too narrow to cut.
void refuseAsTooNarrow(LineVerdict *verdict, const Group &group)
{
    verdict->state = LineVerdict::State::Refused;
    verdict->reason = LineVerdict::Reason::TooNarrow;
    verdict->verse = group.places.constFirst().verse;
    verdict->word = group.places.constFirst().word;
}

} // namespace

QList<LineVerdict> lineVerdicts(
    const TranscribedPage &page,
    const QSize &imageSize,
    const QSize &boxSize)
{
    QList<LineVerdict> verdicts;
    if (!imageSize.isValid()) {
        return verdicts;
    }
    const auto scaled = scalingFor(imageSize, boxSize);
    for (const Group &group : linesOf(page)) {
        LineVerdict verdict;
        const Line line = groundTruth(group, &verdict);
        if (!line.isEmpty() && isTooNarrow(inside(scaled(boundsOf(line)), imageSize))) {
            refuseAsTooNarrow(&verdict, group);
        }
        verdicts.append(verdict);
    }
    return verdicts;
}

QString describe(LineVerdict::Reason reason)
{
    switch (reason) {
    case LineVerdict::Reason::None:
        return QString();
    case LineVerdict::Reason::UncheckedWord:
        return QStringLiteral("unchecked");
    case LineVerdict::Reason::WordWithoutBox:
        return QStringLiteral("a word with no box");
    case LineVerdict::Reason::NoReading:
        return QStringLiteral("an empty word");
    case LineVerdict::Reason::LooseWord:
        return QStringLiteral("a typed word beside it");
    case LineVerdict::Reason::MarginInMiddle:
        return QStringLiteral("an unread note mid-line");
    case LineVerdict::Reason::TooNarrow:
        return QStringLiteral("too short to teach");
    }
    return QString();
}

QString folioIdentity(const TranscribedPage &page)
{
    // The published address first: it is the one thing about a scanned folio
    // that no other folio shares. Then where a local picture was opened from,
    // which tells two `1.jpg`s from two folders apart.
    for (const QString &candidate :
         {page.imageUrl, page.sourcePath, page.imageLabel, page.imageName}) {
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    return QString();
}

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
    const auto scaled = scalingFor(imageSize, boxSize);
    // Through the same rectangle scaling the boxes go through, so a point and a
    // box that touched on the screen still touch on the master.
    const auto scaledPoints = [&scaled](const QList<QPoint> &points) {
        QList<QPointF> out;
        out.reserve(points.size());
        for (const QPoint &point : points) {
            out.append(QPointF(scaled(QRect(point, QSize(1, 1))).topLeft()));
        }
        return out;
    };
    const QRect picture(QPoint(0, 0), imageSize);

    // Keyed by the line number a word carries, which is how a Line finds the
    // geometry that belongs to it.
    QMap<int, const TranscribedLine *> drawnLines;
    for (const TranscribedLine &drawn : page.lines) {
        drawnLines.insert(drawn.index, &drawn);
    }

    const QList<Group> lines = linesOf(page);
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
    // And which folio that picture is, so a set can say what it holds without
    // the name of a file having to carry it.
    const QString identity = folioIdentity(page);
    if (!identity.isEmpty()) {
        xml.writeTextElement(QStringLiteral("fileIdentifier"), identity);
    }
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

    for (const Group &group : lines) {
        LineVerdict verdict;
        const Line line = groundTruth(group, &verdict);
        if (line.isEmpty()) {
            result.verdicts.append(verdict);
            continue;
        }

        const QRect bounds = inside(scaled(boundsOf(line)), imageSize);
        if (isTooNarrow(bounds)) {
            refuseAsTooNarrow(&verdict, group);
            result.verdicts.append(verdict);
            continue;
        }
        result.verdicts.append(verdict);

        // What the segmenter actually drew, where the folio has it. Kraken
        // dewarps the strip along the baseline and masks it to the boundary, so
        // these decide what the model is shown — and Milah's own level line and
        // rectangle are a poor stand-in for a sloping line on a crowded leaf.
        const TranscribedLine *drawn = drawnLines.value(group.detection, nullptr);
        // Clipped only where this line is not the whole of the detection it came
        // from — a marginal note trimmed off an end, or a break the transcriber
        // put in with endsLine because the segmenter ran two manuscript lines
        // together. A line nothing was taken from keeps the shape whole, cut
        // only to the picture.
        //
        // Grown by half its own height before it clips, because the segmenter's
        // outline reaches past the word boxes on purpose, round the ascenders
        // and under the descenders, and squeezing it to their band would shave
        // the very ink the strip is cut for. Two stacked halves therefore still
        // overlap a little; they are no longer the same shape, which is the
        // whole of the difference between one usable strip and two useless ones.
        const bool whole = line.size() == detectionSize.value(group.detection);
        const QRect clip =
            whole ? picture
                  : inside(
                        bounds.adjusted(0, -bounds.height() / 2, 0, bounds.height() / 2),
                        imageSize);

        QString outline;
        if (drawn && drawn->boundary.size() >= 3) {
            const QList<QPointF> ring = clippedOutline(scaledPoints(drawn->boundary), clip);
            if (distinctPoints(ring) >= 3) {
                outline = pointsText(ring);
            }
        }
        // Without a drawn outline the rectangle round the words stands in, and
        // the baseline then has to stay inside that rectangle too — a joined
        // line keeps both detections' baselines and loses its outline, and a
        // baseline running past the shape kraken masks to is one it cannot cut.
        const QRect baselineClip = outline.isEmpty() ? clip.intersected(bounds) : clip;
        if (outline.isEmpty()) {
            outline = polygonOf(bounds);
        }

        QString baseline;
        if (drawn && drawn->baseline.size() >= 2) {
            const QList<QPointF> cut =
                clippedBaseline(scaledPoints(drawn->baseline), baselineClip);
            if (distinctPoints(cut) >= 2) {
                baseline = pointsText(cut);
            }
        }
        if (baseline.isEmpty()) {
            baseline = baselineOf(bounds);
        }

        xml.writeStartElement(QStringLiteral("TextLine"));
        // The recogniser's line and the piece of it, which is what the line
        // view numbers lines by — so a strip refused in the preview can be
        // found again on the folio.
        xml.writeAttribute(
            QStringLiteral("ID"),
            QStringLiteral("line_%1_%2").arg(group.detection).arg(group.part));
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
        // folio nobody has been through yet has to be able to say — and so
        // does why each line was not written.
        TrainingPage none;
        none.candidates = result.candidates;
        none.verdicts = result.verdicts;
        return none;
    }
    result.alto = document;
    return result;
}

} // namespace milah
