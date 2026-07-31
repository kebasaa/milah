#include "test_data.h"

#include <QMap>

namespace milah_test {

const char *const kSampleOsis = R"OSIS(<?xml version="1.0" encoding="UTF-8"?>
<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
  <osisText osisIDWork="Test" osisRefWork="bible" xml:lang="he">
    <header><work osisWork="Test"><title>Test witness</title></work></header>
    <div type="book" osisID="Matt"><chapter osisID="Matt.1">
      <verse osisID="Matt.1.1">ספר <note n="1">A comment</note>הולדת</verse>
    </chapter></div>
  </osisText>
</osis>)OSIS";

const char *const kXmlDeclaration = R"(<?xml version="1.0" encoding="UTF-8"?>)";

const char *const kDoctypeDeclaration =
    R"(<!DOCTYPE osis [<!ENTITY xxe SYSTEM "file:///secret">]>)";

const char *const kApparatusManuscript = R"OSIS(<?xml version="1.0" encoding="UTF-8"?>
<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
  <osisText osisIDWork="Sloane" osisRefWork="bible" xml:lang="he">
    <header><work osisWork="Sloane"><title>Sloane 273</title></work></header>
    <div type="book" osisID="Rev" canonical="true"><div type="introduction" canonical="true"><milestone type="pb" n="1v"/><title type="main" canonical="true">חזון יוחנן<note n="1">An incipit note</note></title></div><chapter sID="Rev.1" osisID="Rev.1" n="1"/><verse sID="Rev.1.1" osisID="Rev.1.1" n="1" subType="x-alt-1"/>ספר <note type="explanation" n="2">A comment</note>הולדת<milestone type="pb" n="2r"/><verse eID="Rev.1.1"/><chapter eID="Rev.1"/><title type="chapter" canonical="true">השער שני</title><chapter sID="Rev.2" osisID="Rev.2" n="2"/><verse sID="Rev.2.1" osisID="Rev.2.1" n="1" subType="x-alt-9"/>דבר<milestone type="x-ms-verse" n="8"/><verse eID="Rev.2.1"/><chapter eID="Rev.2"/></div>
  </osisText>
</osis>)OSIS";

QString witnessOsis(const QString &id, const QString &text)
{
    static const char *const kTemplate =
        R"OSIS(<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
      <osisText osisIDWork="%1" osisRefWork="bible">
        <header><work osisWork="%1"><title>%1</title></work></header>
        <div type="book" osisID="Matt"><chapter osisID="Matt.1">
          <verse osisID="Matt.1.1">%2</verse>
        </chapter></div>
      </osisText>
    </osis>)OSIS";

    return QString::fromUtf8(kTemplate).arg(id, text);
}

QString witnessOsisCovering(const QString &id, const QStringList &chapters)
{
    // Chapters are grouped under the book they belong to, in the order they are
    // first named, so the document is shaped the way a real one is rather than
    // relying on the parser to tolerate a loose one.
    QStringList books;
    QMap<QString, QStringList> byBook;
    for (const QString &chapter : chapters) {
        const QString book = chapter.section(QLatin1Char('.'), 0, 0);
        if (!byBook.contains(book)) {
            books.append(book);
        }
        byBook[book].append(chapter);
    }

    QString divs;
    for (const QString &book : books) {
        QString inner;
        for (const QString &chapter : byBook.value(book)) {
            inner += QStringLiteral(
                         "<chapter osisID=\"%1\">"
                         "<verse osisID=\"%1.1\">דבר</verse>"
                         "</chapter>")
                         .arg(chapter);
        }
        divs += QStringLiteral("<div type=\"book\" osisID=\"%1\">%2</div>")
                    .arg(book, inner);
    }

    static const char *const kTemplate =
        R"OSIS(<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace">
      <osisText osisIDWork="%1" osisRefWork="bible">
        <header><work osisWork="%1"><title>%1</title></work></header>
        %2
      </osisText>
    </osis>)OSIS";

    return QString::fromUtf8(kTemplate).arg(id, divs);
}

} // namespace milah_test
