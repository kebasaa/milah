#pragma once

#include "core/types.h"

#include <QHash>
#include <QString>

class QJsonObject;

namespace milah {

/// How a source numbers the verses of the Hebrew Bible. The two schemes agree
/// almost everywhere and disagree in a few dozen well-known places — Malachi
/// has three chapters in Hebrew and four in English, Joel four and three, and a
/// psalm's heading is verse 1 in Hebrew and no verse at all in English. Read
/// against each other unconverted, the witnesses compare the wrong verses
/// there, every time.
enum class Numbering { English, Hebrew };

/// What `document` follows: its header's refSystem where it names one
/// (Bible.Leningrad, Bible.WLC, Bible.BHS, Bible.MT are Hebrew; Bible.KJV and
/// the English editions are English), otherwise its language — a Hebrew text
/// is taken to be numbered as the Hebrew Bible is, anything else as the
/// English Bibles are.
Numbering numberingOf(const SourceDocument &document);

/// The correspondence between the two schemes, from data/versification.json.
class Versification
{
public:
    static const Versification &shared();
    static Versification fromJson(const QJsonObject &document);

    /// The Hebrew id for an English-numbered one ("Mal.4.1" → "Mal.3.19"), or
    /// `englishId` itself where the two agree. An English psalm heading, which
    /// Milah keys as verse 0, is the Hebrew verse 1.
    QString toHebrew(const QString &englishId) const;

    bool isEmpty() const { return m_shifts.isEmpty() && m_psalmOffsets.isEmpty(); }

private:
    QHash<QString, QString> m_shifts;
    QHash<int, int> m_psalmOffsets;
};

/// Renumbers an English-numbered document's Hebrew Bible verses as the Hebrew
/// Bible numbers them, so it collates against Hebrew witnesses verse for verse.
/// Each renumbered verse keeps its own number as its altNumber ("4:1"), which
/// is what the verse view shows beside the shared one. Does nothing to a
/// document numbered the Hebrew way, or to New Testament books. A verse whose
/// new id is already taken keeps its own. Returns how many were renumbered.
int toHebrewNumbering(SourceDocument &document,
                      const Versification &map = Versification::shared());

} // namespace milah
