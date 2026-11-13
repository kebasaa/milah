#include "core/versification.h"

#include "core/books.h"
#include "core/data_paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace milah {
namespace {

/// "32.1-32" → chapter 32, verses 1 to 32. False for anything else.
bool parseRun(const QString &text, int *chapter, int *first, int *last)
{
    const QString chapterPart = text.section(QLatin1Char('.'), 0, 0);
    const QString versePart = text.section(QLatin1Char('.'), 1);
    bool okChapter = false;
    bool okFirst = false;
    bool okLast = true;
    *chapter = chapterPart.toInt(&okChapter);
    *first = versePart.section(QLatin1Char('-'), 0, 0).toInt(&okFirst);
    *last = versePart.contains(QLatin1Char('-'))
        ? versePart.section(QLatin1Char('-'), 1).toInt(&okLast)
        : *first;
    return okChapter && okFirst && okLast && *last >= *first;
}

QString rewriteId(const QString &id, const QString &from, const QString &to)
{
    return id.startsWith(from) ? to + id.mid(from.size()) : id;
}

} // namespace

Numbering numberingOf(const SourceDocument &document)
{
    const QString system = document.metadata.refSystem.toLower();
    for (const char *hebrew : {"leningrad", "wlc", "bhs", "masoretic", "bible.mt", "hebrew"}) {
        if (system.contains(QLatin1String(hebrew))) {
            return Numbering::Hebrew;
        }
    }
    if (!system.isEmpty() && system.startsWith(QLatin1String("bible."))) {
        return Numbering::English;
    }
    const QString language = document.metadata.language.toLower();
    return language.startsWith(QLatin1String("he")) || language.startsWith(QLatin1String("hbo"))
        ? Numbering::Hebrew
        : Numbering::English;
}

const Versification &Versification::shared()
{
    static const Versification map = [] {
        QFile file(locateDataFile(QStringLiteral("versification.json")));
        if (!file.open(QIODevice::ReadOnly)) {
            return Versification();
        }
        return fromJson(QJsonDocument::fromJson(file.readAll()).object());
    }();
    return map;
}

Versification Versification::fromJson(const QJsonObject &document)
{
    Versification map;
    for (const QJsonValue &value : document.value(QStringLiteral("shifts")).toArray()) {
        const QJsonObject shift = value.toObject();
        const QString book = shift.value(QStringLiteral("book")).toString();
        int englishChapter = 0;
        int englishFirst = 0;
        int englishLast = 0;
        int hebrewChapter = 0;
        int hebrewFirst = 0;
        int hebrewLast = 0;
        if (book.isEmpty()
            || !parseRun(shift.value(QStringLiteral("english")).toString(), &englishChapter,
                         &englishFirst, &englishLast)
            || !parseRun(shift.value(QStringLiteral("hebrew")).toString(), &hebrewChapter,
                         &hebrewFirst, &hebrewLast)
            || englishLast - englishFirst != hebrewLast - hebrewFirst) {
            // A run whose two halves are not the same length is a mistake in
            // the table, and guessing which half is right would misplace
            // verses silently.
            continue;
        }
        for (int offset = 0; offset <= englishLast - englishFirst; ++offset) {
            map.m_shifts.insert(
                QStringLiteral("%1.%2.%3").arg(book).arg(englishChapter).arg(englishFirst + offset),
                QStringLiteral("%1.%2.%3").arg(book).arg(hebrewChapter).arg(hebrewFirst + offset));
        }
    }
    const QJsonObject titles = document.value(QStringLiteral("psalmTitles")).toObject();
    for (auto entry = titles.constBegin(); entry != titles.constEnd(); ++entry) {
        const int offset = entry.key().toInt();
        if (offset <= 0) {
            continue;
        }
        for (const QJsonValue &psalm : entry.value().toArray()) {
            map.m_psalmOffsets.insert(psalm.toInt(), offset);
        }
    }
    return map;
}

QString Versification::toHebrew(const QString &englishId) const
{
    const auto shifted = m_shifts.constFind(englishId);
    if (shifted != m_shifts.constEnd()) {
        return shifted.value();
    }
    if (englishId.startsWith(QLatin1String("Ps."))) {
        const int psalm = englishId.section(QLatin1Char('.'), 1, 1).toInt();
        const auto offset = m_psalmOffsets.constFind(psalm);
        if (offset != m_psalmOffsets.constEnd()) {
            bool numeric = false;
            const int verse = englishId.section(QLatin1Char('.'), 2).toInt(&numeric);
            if (numeric && verse >= 1) {
                return QStringLiteral("Ps.%1.%2").arg(psalm).arg(verse + offset.value());
            }
            // The heading, which Milah keys as verse 0, is the Hebrew verse 1.
            // A two-verse heading has no single verse to be, so it is left.
            if (numeric && verse == 0 && offset.value() == 1) {
                return QStringLiteral("Ps.%1.1").arg(psalm);
            }
        }
    }
    return englishId;
}

int toHebrewNumbering(SourceDocument &document, const Versification &map)
{
    if (map.isEmpty() || numberingOf(document) == Numbering::Hebrew) {
        return 0;
    }

    // Worked out first and applied after, so two verses swapping numbers — as
    // a whole chapter shifting by one does — never collide with each other.
    QHash<QString, QString> renamed;
    QSet<QString> taken;
    for (const SourceVerse &verse : document.verses) {
        taken.insert(verse.reference.id);
    }
    for (const SourceVerse &verse : document.verses) {
        if (testamentOf(verse.reference.book) != Testament::Old) {
            continue;
        }
        const QString hebrew = map.toHebrew(verse.reference.id);
        if (hebrew != verse.reference.id) {
            renamed.insert(verse.reference.id, hebrew);
        }
    }
    // A new id another verse keeps — one that is not itself moving — would be
    // a duplicate, so that verse stays where it is.
    for (auto entry = renamed.begin(); entry != renamed.end();) {
        if (taken.contains(entry.value()) && !renamed.contains(entry.value())) {
            entry = renamed.erase(entry);
        } else {
            ++entry;
        }
    }
    if (renamed.isEmpty()) {
        return 0;
    }

    document.verseIndex.clear();
    for (int index = 0; index < document.verses.size(); ++index) {
        SourceVerse &verse = document.verses[index];
        const QString from = verse.reference.id;
        const QString to = renamed.value(from);
        if (!to.isEmpty()) {
            if (!verse.altNumber) {
                verse.altNumber = QStringLiteral("%1:%2")
                                      .arg(verse.reference.chapter)
                                      .arg(verse.reference.verse);
            }
            verse.reference.id = to;
            verse.reference.chapter = to.section(QLatin1Char('.'), 1, 1).toInt();
            verse.reference.verse = to.section(QLatin1Char('.'), 2);
            verse.label = verse.reference.verse;
            for (SourceToken &token : verse.tokens) {
                token.id = rewriteId(token.id, from + QLatin1Char(':'), to + QLatin1Char(':'));
                for (SourceNote &note : token.notes) {
                    note.id = rewriteId(note.id, from + QLatin1Char(':'), to + QLatin1Char(':'));
                }
            }
        }
        document.verseIndex.insert(verse.reference.id, index);
    }
    for (SourceMilestone &milestone : document.milestones) {
        if (milestone.verseId && renamed.contains(*milestone.verseId)) {
            milestone.verseId = renamed.value(*milestone.verseId);
        }
    }
    QHash<QString, QString> joined;
    for (auto entry = document.joinedVerses.constBegin(); entry != document.joinedVerses.constEnd();
         ++entry) {
        joined.insert(map.toHebrew(entry.key()), renamed.value(entry.value(), entry.value()));
    }
    document.joinedVerses = joined;
    return int(renamed.size());
}

} // namespace milah
