#include "core/page_layout.h"

#include <QFile>
#include <QtTest>

#include <algorithm>

using namespace milah;

namespace {

/// moc deletes everything after `//` inside a multi-line raw string, so a
/// namespace URL cannot be written out in a fixture: `http://www.loc.gov/…`
/// reaches the compiler as `http:` and the fixture stops being XML. Every
/// namespace below therefore says SLASHES where the scheme's two slashes go.
QByteArray fixture(QString xml)
{
    return xml.replace(QStringLiteral("SLASHES"), QStringLiteral("//")).toUtf8();
}

/// An ALTO shaped the way kraken's own template writes one: pixels, a Page
/// carrying the image size, and one String per word inside a TextLine.
QByteArray krakenAlto()
{
    return fixture(QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<alto xmlns="http:SLASHESwww.loc.gov/standards/alto/ns-v4#">
  <Description>
    <MeasurementUnit>pixel</MeasurementUnit>
    <sourceImageInformation><fileName>folio.jpg</fileName></sourceImageInformation>
  </Description>
  <Layout>
    <Page ID="page_0" PHYSICAL_IMG_NR="0" WIDTH="1000" HEIGHT="1400">
      <PrintSpace HPOS="0" VPOS="0" WIDTH="1000" HEIGHT="1400">
        <TextBlock ID="block_0" HPOS="100" VPOS="200" WIDTH="800" HEIGHT="300">
          <TextLine ID="line_0" HPOS="100" VPOS="200" WIDTH="800" HEIGHT="60">
            <String ID="segment_0" CONTENT="בראשית" HPOS="700" VPOS="200" WIDTH="180" HEIGHT="55" WC="0.98"/>
            <SP ID="sp_0" HPOS="680" VPOS="200" WIDTH="20"/>
            <String ID="segment_1" CONTENT="ברא" HPOS="560" VPOS="205" WIDTH="110" HEIGHT="50"/>
          </TextLine>
          <TextLine ID="line_1" HPOS="100" VPOS="270" WIDTH="800" HEIGHT="60">
            <String ID="segment_2" CONTENT="אלהים" HPOS="690" VPOS="270" WIDTH="150" HEIGHT="52"/>
          </TextLine>
        </TextBlock>
      </PrintSpace>
    </Page>
  </Layout>
</alto>
)"));
}

/// PAGE as eScriptorium exports it when words are asked for: outlines rather
/// than rectangles, and a line that also states its own text.
QByteArray wordSegmentedPage()
{
    return fixture(QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<PcGts xmlns="http:SLASHESschema.primaresearch.org/PAGE/gts/pagecontent/2019-07-15">
  <Metadata><Creator>eScriptorium</Creator></Metadata>
  <Page imageFilename="folio.jpg" imageWidth="1000" imageHeight="1400">
    <TextRegion id="r0">
      <Coords points="100,200 900,200 900,500 100,500"/>
      <TextLine id="l0">
        <Coords points="100,200 900,200 900,260 100,260"/>
        <Word id="w0">
          <Coords points="700,200 880,205 875,255 700,250"/>
          <TextEquiv><Unicode>בראשית</Unicode></TextEquiv>
        </Word>
        <Word id="w1">
          <Coords points="560,205 670,205 670,255 560,255"/>
          <TextEquiv><Unicode>ברא</Unicode></TextEquiv>
        </Word>
        <TextEquiv><Unicode>בראשית ברא</Unicode></TextEquiv>
      </TextLine>
    </TextRegion>
  </Page>
</PcGts>
)"));
}

} // namespace

/// The ALTO and PAGE reader. Everything the overlay draws comes through here,
/// and a coordinate read wrongly does not look like a bug — it looks like a
/// recogniser that read the wrong part of the page. So the refusals matter as
/// much as the successes.
class PageLayoutTest final : public QObject
{
    Q_OBJECT

private slots:
    void altoYieldsItsWordsInOrder()
    {
        QString error;
        const RecognisedPage page = parseRecognisedPage(krakenAlto(), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.imageSize, QSize(1000, 1400));
        QCOMPARE(page.words.size(), 3);
        QCOMPARE(page.words.at(0).text, QStringLiteral("בראשית"));
        QCOMPARE(page.words.at(1).text, QStringLiteral("ברא"));
        QCOMPARE(page.words.at(2).text, QStringLiteral("אלהים"));
    }

    void anAltoWordKeepsItsBox()
    {
        const RecognisedPage page = parseRecognisedPage(krakenAlto(), nullptr);
        QCOMPARE(page.words.at(0).box, QRect(700, 200, 180, 55));
        QCOMPARE(page.words.at(1).box, QRect(560, 205, 110, 50));
    }

    void wordsRememberWhichLineTheyCameFrom()
    {
        // Hebrew runs right to left, so a word further left is not a word
        // further on. Line membership is the only thing that says where one
        // line of the folio ends.
        const RecognisedPage page = parseRecognisedPage(krakenAlto(), nullptr);
        QCOMPARE(page.words.at(0).line, 0);
        QCOMPARE(page.words.at(1).line, 0);
        QCOMPARE(page.words.at(2).line, 1);
    }

    void spacesAreNotWords()
    {
        // ALTO writes <SP/> between strings. Read as a word it would put a blank
        // cell in the grid between every pair of real ones.
        const RecognisedPage page = parseRecognisedPage(krakenAlto(), nullptr);
        for (const RecognisedWord &word : page.words) {
            QVERIFY(!word.text.isEmpty());
        }
    }

    void aDecimalCoordinateIsRounded()
    {
        const QByteArray alto = fixture(QStringLiteral(R"(<alto>
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page WIDTH="1000" HEIGHT="1400"><PrintSpace><TextBlock><TextLine>
    <String CONTENT="ברא" HPOS="560.4" VPOS="205.6" WIDTH="110.2" HEIGHT="50.5"/>
  </TextLine></TextBlock></PrintSpace></Page></Layout>
</alto>
)"));
        const RecognisedPage page = parseRecognisedPage(alto, nullptr);
        QCOMPARE(page.words.size(), 1);
        QCOMPARE(page.words.constFirst().box, QRect(560, 206, 110, 51));
    }

    void aWordWithNoGeometryKeepsItsText()
    {
        // The transcription is the point and the picture is the check, so a
        // string missing its box is worth reading in — it simply does not get
        // drawn on the folio.
        const QByteArray alto = fixture(QStringLiteral(R"(<alto>
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page WIDTH="1000" HEIGHT="1400"><PrintSpace><TextBlock><TextLine>
    <String CONTENT="ברא"/>
  </TextLine></TextBlock></PrintSpace></Page></Layout>
</alto>
)"));
        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.words.size(), 1);
        QVERIFY(page.words.constFirst().box.isNull());
    }

    void pagePolygonsBecomeTheirBoundingRectangles()
    {
        QString error;
        const RecognisedPage page = parseRecognisedPage(wordSegmentedPage(), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.imageSize, QSize(1000, 1400));
        QCOMPARE(page.words.size(), 2);
        QCOMPARE(page.words.constFirst().text, QStringLiteral("בראשית"));
        // 700..880 across and 200..255 down, which is the outline squared off.
        QCOMPARE(page.words.constFirst().box, QRect(700, 200, 181, 56));
    }

    void aLineIsNotReadAsAWord()
    {
        // The line in the fixture states its own text as well as its words'. If
        // that were taken too, the grid would hold the whole line a second time.
        const RecognisedPage page = parseRecognisedPage(wordSegmentedPage(), nullptr);
        QCOMPARE(page.words.size(), 2);
    }

    void tenthsOfAMillimetreAreRefused()
    {
        // Legal ALTO, and unplaceable without the scan resolution. Drawn as
        // pixels the boxes would cluster in one corner and look like a
        // recogniser that had gone wrong rather than a reader that had.
        const QByteArray alto = fixture(QStringLiteral(R"(<alto>
  <Description><MeasurementUnit>mm10</MeasurementUnit></Description>
  <Layout><Page WIDTH="2100" HEIGHT="2970"><PrintSpace><TextBlock><TextLine>
    <String CONTENT="ברא" HPOS="100" VPOS="100" WIDTH="50" HEIGHT="20"/>
  </TextLine></TextBlock></PrintSpace></Page></Layout>
</alto>
)"));
        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("mm10")));
        QVERIFY(page.words.isEmpty());
    }

    void aPageSegmentedIntoLinesOnlySaysSo()
    {
        // The commoner of the two PAGE exports, and no use to the overlay. An
        // empty result would read as a recogniser that found nothing.
        const QByteArray xml = fixture(QStringLiteral(R"(<?xml version="1.0"?>
<PcGts xmlns="http:SLASHESschema.primaresearch.org/PAGE/gts/pagecontent/2019-07-15">
  <Page imageFilename="folio.jpg" imageWidth="1000" imageHeight="1400">
    <TextRegion id="r0">
      <TextLine id="l0">
        <Coords points="100,200 900,200 900,260 100,260"/>
        <TextEquiv><Unicode>בראשית ברא אלהים</Unicode></TextEquiv>
      </TextLine>
    </TextRegion>
  </Page>
</PcGts>
)"));
        QString error;
        const RecognisedPage page = parseRecognisedPage(xml, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("word")));
        QVERIFY(page.words.isEmpty());
    }

    void aPageWithNoSizeIsRefused()
    {
        const QByteArray alto = fixture(QStringLiteral(R"(<alto>
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page><PrintSpace><TextBlock><TextLine>
    <String CONTENT="ברא" HPOS="560" VPOS="205" WIDTH="110" HEIGHT="50"/>
  </TextLine></TextBlock></PrintSpace></Page></Layout>
</alto>
)"));
        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(page.words.isEmpty());
    }

    void anEmptyPageIsNotAnError()
    {
        // A folio the segmenter found nothing on — a blank verso, a photograph
        // of a binding. Nothing went wrong; there is simply nothing there.
        const QByteArray alto = fixture(QStringLiteral(R"(<alto>
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page WIDTH="1000" HEIGHT="1400"><PrintSpace/></Page></Layout>
</alto>
)"));
        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(page.words.isEmpty());
        QCOMPARE(page.imageSize, QSize(1000, 1400));
    }

    void malformedXmlSaysWhereRatherThanThrowing()
    {
        QString error;
        const RecognisedPage page =
            parseRecognisedPage(QByteArrayLiteral("<alto><Layout></alto>"), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(page.words.isEmpty());
    }

    void somethingElseEntirelyIsNamed()
    {
        QString error;
        const RecognisedPage page = parseRecognisedPage(
            QByteArrayLiteral("<osis><osisText/></osis>"), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("osis")));
    }

    void nothingAtAllIsAnError()
    {
        QString error;
        parseRecognisedPage(QByteArray(), &error);
        QVERIFY(!error.isEmpty());
    }

    void aDoctypeIsRefused()
    {
        // The same refusal parseOsis() makes. This file came from outside Milah
        // too, and an external entity is a way of making a parser read something
        // nobody handed it.
        QString error;
        parseRecognisedPage(
            QByteArrayLiteral("<!DOCTYPE alto SYSTEM \"alto.dtd\"><alto/>"), &error);
        QVERIFY(!error.isEmpty());
    }

    void aRealKrakenFileReadsAsItShould()
    {
        // Every other fixture here was written by hand from kraken's template.
        // This one is a file kraken actually produced, handed over through
        // MILAH_ALTO, so the reader is checked against the thing rather than
        // against my reading of the thing.
        const QByteArray where = qgetenv("MILAH_ALTO");
        if (where.isEmpty()) {
            QSKIP("Set MILAH_ALTO to a kraken ALTO file to run this.");
        }
        QFile file(QString::fromLocal8Bit(where));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

        QString error;
        const RecognisedPage page = parseRecognisedPage(file.readAll(), &error);
        qInfo() << "error:" << error;
        qInfo() << "image size:" << page.imageSize;
        qInfo() << "words:" << page.words.size();
        for (int index = 0; index < std::min<qsizetype>(5, page.words.size()); ++index) {
            qInfo() << "  " << page.words.at(index).text << page.words.at(index).box
                    << "line" << page.words.at(index).line;
        }
        QVERIFY(error.isEmpty());
        QVERIFY(!page.words.isEmpty());
    }

    void theErrorMayBeIgnored()
    {
        // The overlay's own refresh path passes nothing, so a null must not be
        // written through.
        parseRecognisedPage(QByteArrayLiteral("<nonsense/>"), nullptr);
    }

    /// The line the segmenter actually drew, which is what a recogniser cuts
    /// its training strips from.
    ///
    /// Kraken writes a sloping, many-point baseline and a boundary that follows
    /// the ink. Milah used to read neither and rebuild both from the word boxes
    /// -- a level line through their middles and a rectangle round them. On 158r
    /// of MS Oo.1.32 a real baseline falls a median of 12 pixels across a leaf
    /// whose letters are some 30 tall, and a real boundary covers 0.65 of its
    /// bounding rectangle, so that substitution shears the strip and fills it
    /// with the neighbouring lines' ascenders.
    void theLineTheSegmenterDrewIsKept()
    {
        const QByteArray alto = fixture(QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<alto xmlns="http:SLASHESwww.loc.gov/standards/alto/ns-v4#">
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page WIDTH="1000" HEIGHT="1400"><PrintSpace HPOS="0" VPOS="0" WIDTH="1000" HEIGHT="1400">
    <TextBlock ID="b">
      <TextLine ID="l0" BASELINE="588 73 658 72 720 66">
        <Shape><Polygon POINTS="597 54 660 57 666 93 679 103 597 95"/></Shape>
        <String CONTENT="alpha" HPOS="600" VPOS="55" WIDTH="50" HEIGHT="30"/>
      </TextLine>
    </TextBlock>
  </PrintSpace></Page></Layout>
</alto>)"));

        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.lines.size(), 1);
        QCOMPARE(page.lines.first().index, 0);
        // Sloping, and every point of it: two would be the invention this
        // replaces.
        QCOMPARE(page.lines.first().baseline.size(), 3);
        QCOMPARE(page.lines.first().baseline.first(), QPoint(588, 73));
        QCOMPARE(page.lines.first().baseline.last(), QPoint(720, 66));
        QCOMPARE(page.lines.first().boundary.size(), 5);
        QCOMPARE(page.lines.first().boundary.at(3), QPoint(679, 103));
    }

    /// A word's own outline is not the line's. Kraken gives a String no Shape,
    /// but a writer that did would otherwise leave the line outlined round its
    /// last word.
    void aWordsOutlineIsNotTheLines()
    {
        const QByteArray alto = fixture(QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<alto xmlns="http:SLASHESwww.loc.gov/standards/alto/ns-v4#">
  <Description><MeasurementUnit>pixel</MeasurementUnit></Description>
  <Layout><Page WIDTH="1000" HEIGHT="1400"><PrintSpace HPOS="0" VPOS="0" WIDTH="1000" HEIGHT="1400">
    <TextBlock ID="b">
      <TextLine ID="l0" BASELINE="100 50 900 50">
        <Shape><Polygon POINTS="100 20 900 20 900 60 100 60"/></Shape>
        <String CONTENT="alpha" HPOS="600" VPOS="25" WIDTH="50" HEIGHT="30">
          <Shape><Polygon POINTS="600 25 650 25 650 55 600 55"/></Shape>
        </String>
      </TextLine>
    </TextBlock>
  </PrintSpace></Page></Layout>
</alto>)"));

        QString error;
        const RecognisedPage page = parseRecognisedPage(alto, &error);
        QCOMPARE(page.lines.size(), 1);
        QCOMPARE(page.lines.first().boundary.size(), 4);
        QCOMPARE(page.lines.first().boundary.first(), QPoint(100, 20));
    }

    /// A file that records none reads as none, rather than as a line at the
    /// origin -- which would put a training strip in the corner of the folio.
    void aFileWithNoLineGeometryHasNone()
    {
        QString error;
        const RecognisedPage page = parseRecognisedPage(krakenAlto(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!page.words.isEmpty());
        for (const RecognisedLine &line : page.lines) {
            QVERIFY(line.isEmpty());
        }
    }
};

QTEST_MAIN(PageLayoutTest)
#include "page_layout_test.moc"
