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
        // readable as the shelfmark it came from — and a hash of the shelfmark
        // as written follows, because that alone is not unique.
        const QRegularExpression hashed(QStringLiteral("^MS_Oo\\.1\\.32-[0-9a-f]{8}$"));
        QVERIFY2(hashed.match(TrainingSet::slugFor(cochin())).hasMatch(),
                 qPrintable(TrainingSet::slugFor(cochin())));
        QCOMPARE(TrainingSet::labelFor(cochin()), QStringLiteral("MS Oo.1.32"));

        // No shelfmark: the manuscript's name, which is better than nothing.
        TranscriptionMetadata named;
        named.manuscriptName = QStringLiteral("1 John");
        QVERIFY(TrainingSet::slugFor(named).startsWith(QStringLiteral("1_John-")));

        // And nothing at all still has to go somewhere.
        QCOMPARE(TrainingSet::slugFor({}), QStringLiteral("unnamed"));
    }

    /// Two shelfmarks that clean to the same folder name are two sets. Every
    /// shelfmark written in Hebrew cleans to nothing, and they all used to
    /// share one.
    void shelfmarksThatLookAlikeAreTwoSets()
    {
        TranscriptionMetadata first;
        first.shelfmark = QString::fromUtf8("\xd7\x9b\xd7\xaa\xd7\x91 \xd7\x90");
        TranscriptionMetadata second;
        second.shelfmark = QString::fromUtf8("\xd7\x9b\xd7\xaa\xd7\x91 \xd7\x91");
        QVERIFY(TrainingSet::slugFor(first) != TrainingSet::slugFor(second));

        TranscriptionMetadata spaced;
        spaced.shelfmark = QStringLiteral("MS Oo 1 32");
        TranscriptionMetadata underscored;
        underscored.shelfmark = QStringLiteral("MS_Oo_1_32");
        QVERIFY(TrainingSet::slugFor(spaced) != TrainingSet::slugFor(underscored));
    }

    /// **Two folios whose labels clean to the same name are two folios.** A
    /// label in Hebrew has no letter a file name keeps, so every such folio used
    /// to be filed as `folio` — and each save replaced the last one.
    void foliosWithLookAlikeLabelsAreKeptApart()
    {
        const QByteArray image = picture(QSize(400, 200));
        TranscriptionMetadata hebrew;
        hebrew.shelfmark = QStringLiteral("Look-alike labels");

        const TranscribedPage first = folio(QString::fromUtf8("\xd7\x93\xd7\xa3 \xd7\x90"), 3);
        const TranscribedPage second = folio(QString::fromUtf8("\xd7\x93\xd7\xa3 \xd7\x91"), 3);
        QVERIFY(TrainingSet::stemFor(first) != TrainingSet::stemFor(second));

        QCOMPARE(TrainingSet::add(first, hebrew, image), 1);
        QCOMPARE(TrainingSet::add(second, hebrew, image), 1);
        QCOMPARE(TrainingSet::contentsOf(TrainingSet::slugFor(hebrew)).folios, 2);

        // And the set can say which is which, by the labels they were saved
        // under rather than by file names.
        const QList<TrainingSet::Folio> folios =
            TrainingSet::foliosOf(TrainingSet::slugFor(hebrew));
        QCOMPARE(folios.size(), 2);
        QStringList labels;
        for (const TrainingSet::Folio &held : folios) {
            labels.append(held.label);
            QCOMPARE(held.lines, 1);
        }
        QVERIFY(labels.contains(first.imageLabel));
        QVERIFY(labels.contains(second.imageLabel));
    }

    /// A folio whose lines stopped being ground truth is taken out of the set
    /// when it is saved again, instead of the set going on teaching the old
    /// reading with no way to stop it.
    void savingAFolioWithNothingFinishedWithdrawsIt()
    {
        const QByteArray image = picture(QSize(400, 200));
        TranscribedPage page = folio(QStringLiteral("155r"), 3);
        QCOMPARE(TrainingSet::add(page, cochin(), image), 1);
        QVERIFY(TrainingSet::savedFolio(page, cochin()).present);

        for (TranscribedWord &word : page.verses[0].words) {
            word.unchecked = true;
        }
        QCOMPARE(TrainingSet::add(page, cochin(), image), 0);
        QVERIFY(!TrainingSet::savedFolio(page, cochin()).present);
        const QDir folder(TrainingSet::directoryOf(TrainingSet::slugFor(cochin())));
        QVERIFY(!folder.exists(TrainingSet::stemFor(page) + QStringLiteral(".jpg")));
    }

    /// **A folio saved a moment ago is not stale.** The check rebuilds what
    /// would be written now, and used to do it against a picture of a nominal
    /// thousand pixels square: every box further down the leaf than that was
    /// clamped to nothing and dropped, and the panel called the folio changed.
    /// It rebuilds at the saved file's own size now, carried across from the
    /// picture the boxes are in, exactly as the save was.
    void aWideFolioSavedAMomentAgoIsNotStale()
    {
        TranscriptionMetadata tall;
        tall.shelfmark = QStringLiteral("Tall folio");
        TranscribedPage page;
        page.imageLabel = QStringLiteral("156r");
        page.imageName = page.imageLabel;
        TranscribedVerse verse;
        for (int line = 0; line < 3; ++line) {
            for (int index = 0; index < 3; ++index) {
                verse.words.append(read("\xd7\x91\xd7\xa8\xd7\x90", line,
                                        QRect(1200 + index * 120, 400 + line * 400, 100, 40)));
            }
        }
        page.verses = {verse};

        // A local scan, wider than a thousand pixels, with its text past the
        // thousandth column: saved as it stands, and again from a master three
        // times the size of it.
        const QSize onScreen(2400, 3200);
        QCOMPARE(TrainingSet::add(page, tall, picture(onScreen)), 3);
        QVERIFY(!TrainingSet::savedFolio(page, tall).stale);

        QCOMPARE(TrainingSet::add(page, tall, picture(QSize(7200, 9600)), onScreen), 3);

        const TrainingSet::Saved saved = TrainingSet::savedFolio(page, tall, onScreen);
        QVERIFY(saved.present);
        QCOMPARE(saved.lines, 3);
        QVERIFY(!saved.stale);
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

    /// A folio the set has never held is not in it, and nothing is claimed
    /// about it. The panel shows the plain "Save this folio" for this case.
    void aFolioNeverSavedIsNotInTheSet()
    {
        const TrainingSet::Saved saved =
            TrainingSet::savedFolio(folio(QStringLiteral("160r"), 3), cochin());
        QVERIFY(!saved.present);
        QVERIFY(!saved.stale);
        QCOMPARE(saved.lines, 0);
    }

    /// Saved and untouched since: the set holds what the folio says, and there
    /// is nothing to do about it.
    void aFolioSavedAndLeftAloneIsNotStale()
    {
        const QByteArray image = picture(QSize(400, 200));
        const TranscribedPage page = folio(QStringLiteral("151r"), 3);
        QCOMPARE(TrainingSet::add(page, cochin(), image), 1);

        const TrainingSet::Saved saved = TrainingSet::savedFolio(page, cochin());
        QVERIFY(saved.present);
        QVERIFY(!saved.stale);
        QCOMPARE(saved.lines, 1);
    }

    /// **Corrected since it was saved.** The set is still holding the older
    /// reading of a line, and a model taught it learns the older reading — so
    /// the one thing that was ever missing is being told, since saving again
    /// replaces rather than duplicates.
    ///
    /// Compared on the ground truth alone. A save carries the picture the
    /// library gave that day, and the coordinates move with it; the text is
    /// what a correction changes and what a model is taught.
    void aFolioCorrectedSinceIsStale()
    {
        const QByteArray image = picture(QSize(400, 200));
        QCOMPARE(TrainingSet::add(folio(QStringLiteral("152r"), 3), cochin(), image), 1);

        TranscribedPage corrected = folio(QStringLiteral("152r"), 3);
        corrected.verses[0].words[1].hebrew = QString::fromUtf8("\xd7\x90\xd7\x97\xd7\xa8");
        const TrainingSet::Saved saved = TrainingSet::savedFolio(corrected, cochin());
        QVERIFY(saved.present);
        QVERIFY(saved.stale);

        // And saving again settles it.
        QCOMPARE(TrainingSet::add(corrected, cochin(), image), 1);
        QVERIFY(!TrainingSet::savedFolio(corrected, cochin()).stale);
    }

    /// A word added to the folio counts as a correction too — the line's ground
    /// truth is longer than what the set holds, which is a different thing to
    /// teach a model even though every word already there is unchanged.
    void aLineThatGrewIsStale()
    {
        const QByteArray image = picture(QSize(400, 200));
        QCOMPARE(TrainingSet::add(folio(QStringLiteral("153r"), 3), cochin(), image), 1);
        QVERIFY(TrainingSet::savedFolio(folio(QStringLiteral("153r"), 4), cochin()).stale);
    }

    /// The picture is not fetched to answer this. It is asked whenever the
    /// folio changes, and a folio off a library holds an address rather than
    /// bytes — going to the network for it would put a wait behind every
    /// keystroke.
    void askingWhatIsSavedFetchesNothing()
    {
        const QByteArray image = picture(QSize(400, 200));
        TranscribedPage page = folio(QStringLiteral("154r"), 3);
        QCOMPARE(TrainingSet::add(page, cochin(), image), 1);

        // No picture on it at all, and the answer is the same.
        page.imageUrl.clear();
        page.sourcePath.clear();
        const TrainingSet::Saved saved = TrainingSet::savedFolio(page, cochin());
        QVERIFY(saved.present);
        QVERIFY(!saved.stale);
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
                set.slug != TrainingSet::slugFor(other),
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
