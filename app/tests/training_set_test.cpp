#include "core/transcription.h"
#include "ui/training_set.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QtTest>

using namespace milah;

namespace {

TranscribedWord read(const char *hebrew, int line, const QRect &box)
{
    TranscribedWord word;
    word.hebrew = QString::fromUtf8(hebrew);
    word.line = line;
    word.box = box;
    word.unchecked = false;
    return word;
}

/// A real encoded picture, because the set reads the size out of one.
QByteArray picture(const QSize &size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

TranscribedPage folio(const QString &label, int words)
{
    TranscribedPage page;
    page.imageLabel = label;
    page.imageName = label;
    TranscribedVerse verse;
    for (int index = 0; index < words; ++index) {
        verse.words.append(
            read("\xd7\x91\xd7\xa8\xd7\x90", 0, QRect(10 + index * 40, 10, 30, 20)));
    }
    page.verses = {verse};
    return page;
}

TranscriptionMetadata cochin()
{
    TranscriptionMetadata metadata;
    metadata.shelfmark = QStringLiteral("MS Oo.1.32");
    metadata.manuscriptName = QStringLiteral("1 John");
    return metadata;
}

} // namespace

class TrainingSetTest final : public QObject
{
    Q_OBJECT

private slots:
    /// Everything here writes into the real application data directory
    /// otherwise, which would put test folios into somebody's training set.
    ///
    /// And then clears what the last run left. These are counts of what is on
    /// disk, which is the whole point of the thing being tested — a set that
    /// accumulates between sessions accumulates between test runs too.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir root(
            QFileInfo(TrainingSet::directoryOf(QStringLiteral("probe"))).absolutePath());
        QVERIFY(root.removeRecursively());
    }

    /// The shelfmark names the set, because that is what stays the same while
    /// the transcription file changes: Matthew and James of one manuscript are
    /// two files and one hand.
    void theShelfmarkNamesTheSet()
    {
        // archiveNameFragment keeps letters, digits, dots and dashes and turns
        // everything else into an underscore, so the folder stays about as
        // readable as the shelfmark it came from.
        QCOMPARE(TrainingSet::slugFor(cochin()), QStringLiteral("MS_Oo.1.32"));
        QCOMPARE(TrainingSet::labelFor(cochin()), QStringLiteral("MS Oo.1.32"));

        // No shelfmark: the manuscript's name, which is better than nothing.
        TranscriptionMetadata named;
        named.manuscriptName = QStringLiteral("1 John");
        QCOMPARE(TrainingSet::slugFor(named), QStringLiteral("1_John"));

        // And nothing at all still has to go somewhere.
        QCOMPARE(TrainingSet::slugFor({}), QStringLiteral("unnamed"));
    }

    /// A folio saved twice replaces itself.
    ///
    /// Going back over a folio makes a better statement of the same lines, not
    /// a second folio — and a model shown the same line twice, once wrong,
    /// learns the wrong one as readily as the right one.
    void savingAFolioAgainReplacesIt()
    {
        const QByteArray image = picture(QSize(400, 200));

        QCOMPARE(TrainingSet::add(folio(QStringLiteral("150r"), 3), cochin(), image), 1);
        TrainingSet::Set set = TrainingSet::contentsOf(TrainingSet::slugFor(cochin()));
        QCOMPARE(set.folios, 1);
        QCOMPARE(set.lines, 1);
        QCOMPARE(set.label, QStringLiteral("MS Oo.1.32"));

        // The same folio, corrected further.
        QCOMPARE(TrainingSet::add(folio(QStringLiteral("150r"), 5), cochin(), image), 1);
        set = TrainingSet::contentsOf(TrainingSet::slugFor(cochin()));
        QCOMPARE(set.folios, 1);

        // A different one joins it.
        QCOMPARE(TrainingSet::add(folio(QStringLiteral("150v"), 4), cochin(), image), 1);
        set = TrainingSet::contentsOf(TrainingSet::slugFor(cochin()));
        QCOMPARE(set.folios, 2);
        QCOMPARE(set.lines, 2);
        QVERIFY(set.bytes > 0);
    }

    /// A folio with nothing finished on it writes nothing, and does not create
    /// a set that then sits in the list offering nothing to train on.
    void aFolioWithNothingFinishedIsNotSaved()
    {
        TranscribedPage page = folio(QStringLiteral("999r"), 3);
        for (TranscribedWord &word : page.verses[0].words) {
            word.unchecked = true;
        }
        TranscriptionMetadata other;
        other.shelfmark = QStringLiteral("Nothing Here");

        QCOMPARE(TrainingSet::add(page, other, picture(QSize(400, 200))), 0);
        for (const TrainingSet::Set &set : TrainingSet::known()) {
            QVERIFY2(
                set.slug != QStringLiteral("Nothing_Here"),
                "an empty set was listed as trainable");
        }
    }

    /// And a folio with no picture: nothing knows how big the page is, so there
    /// is nothing to measure the boxes against.
    void aFolioWithNoPictureIsNotSaved()
    {
        QCOMPARE(TrainingSet::add(folio(QStringLiteral("1r"), 3), cochin(), QByteArray()), 0);
    }
};

QTEST_MAIN(TrainingSetTest)
#include "training_set_test.moc"
