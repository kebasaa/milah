#include "core/name_forms.h"

#include "core/data_paths.h"
#include "core/hebrew_forms.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

namespace milah {
namespace {

/// Fewest Hebrew letters a spelling may have and still name anything.
///
/// The same three the near-match rung and the skeleton insist on, and for the
/// same reason: two letters are not evidence. A two-letter group would also
/// claim every prefixed particle in the corpus once peeling is allowed.
constexpr int kMinimumForm = 3;

int countsLetters(const QString &text)
{
    int letters = 0;
    for (const QChar character : text) {
        if (isHebrewLetter(character)) {
            ++letters;
        }
    }
    return letters;
}

} // namespace

NameForms NameForms::fromJson(const QJsonObject &document)
{
    NameForms names;

    for (const QJsonValue &value : document.value(QStringLiteral("names")).toArray()) {
        const QJsonObject entry = value.toObject();

        NameGroup group;
        group.id = entry.value(QStringLiteral("id")).toString().trimmed();
        // Normalised, because this file is hand-written: the same pointed word
        // can be typed with its marks in either order, and the unnormalised one
        // would go into the edition looking identical to everything else and
        // comparing unequal to all of it.
        group.preferred = entry.value(QStringLiteral("prefer"))
                              .toString()
                              .trimmed()
                              .normalized(QString::NormalizationForm_C);
        group.note = entry.value(QStringLiteral("note")).toString().trimmed();

        // A group with no id cannot be replaced by an editor's own file, and
        // one with no preferred form has nothing to offer. Dropped rather than
        // half-kept, the way a malformed phrase rule is.
        if (group.id.isEmpty() || group.preferred.isEmpty()) {
            continue;
        }

        // The preferred form is a member of its own group, so a witness reading
        // the Hebrew name aligns with the transliteration without being listed
        // twice.
        QStringList spellings{group.preferred};
        for (const QJsonValue &form : entry.value(QStringLiteral("forms")).toArray()) {
            spellings.append(form.toString());
        }
        for (const QString &spelling : spellings) {
            const QString key = foldedKey(spelling);
            if (countsLetters(key) >= kMinimumForm && !group.keys.contains(key)) {
                group.keys.append(key);
            }
        }

        // Every spelling refused as too short: nothing would ever reach this
        // group.
        if (group.keys.isEmpty()) {
            continue;
        }
        names.m_groups.append(group);
    }

    names.reindex();
    return names;
}

void NameForms::reindex()
{
    m_index.clear();
    for (int index = 0; index < m_groups.size(); ++index) {
        for (const QString &key : m_groups.at(index).keys) {
            // First claim wins, deterministically by table order. A spelling
            // two groups both want is a mistake in the table, and settling it
            // here is better than letting lookup order decide it.
            if (!m_index.contains(key)) {
                m_index.insert(key, index);
            }
        }
    }
}

NameForms NameForms::fromFile(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return NameForms();
    }
    return fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

const NameForms &NameForms::shared()
{
    static const NameForms names = [] {
        NameForms loaded = fromFile(locateDataFile(QStringLiteral("hebrew_names.json")));
        // Names the editor keeps outside the data directory, so the table can
        // grow — or a wrong group be corrected — without touching the shipped
        // file.
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!directory.isEmpty()) {
            loaded.append(fromFile(directory + QStringLiteral("/hebrew_names.json")));
        }
        return loaded;
    }();
    return names;
}

void NameForms::append(const NameForms &other)
{
    for (const NameGroup &group : other.m_groups) {
        int existing = -1;
        for (int index = 0; index < m_groups.size(); ++index) {
            if (m_groups.at(index).id == group.id) {
                existing = index;
                break;
            }
        }
        if (existing >= 0) {
            // Whole, spellings included: the replacement is the group now, and
            // a form it left out is a form the editor took out.
            m_groups[existing] = group;
        } else {
            m_groups.append(group);
        }
    }
    reindex();
}

int NameForms::groupForFoldedKey(const QString &foldedText) const
{
    const auto whole = m_index.constFind(foldedText);
    if (whole != m_index.constEnd()) {
        return whole.value();
    }

    // Shortest peel first, and that is load-bearing rather than an
    // optimisation: ולאדיצאן has to resolve as ו + לאדיצאן, because Laodicea
    // begins with a lamed of its own, while לאודיקיאה has to resolve whole.
    // A greedy two-letter peel gets the first wrong; refusing to peel gets the
    // second right and the first not at all. Trying none, then one, then two
    // is the only order that answers both.
    for (int peeled = 1; peeled <= 2 && peeled < foldedText.size(); ++peeled) {
        if (!isPrefixLetter(foldedText.at(peeled - 1))) {
            break;
        }
        const QString rest = foldedText.mid(peeled);
        if (countsLetters(rest) < kMinimumForm) {
            break;
        }
        const auto found = m_index.constFind(rest);
        if (found != m_index.constEnd()) {
            return found.value();
        }
    }

    return -1;
}

int NameForms::groupFor(const QString &rawText, QString *peeled) const
{
    if (peeled) {
        peeled->clear();
    }

    const QString key = foldedKey(rawText);
    const auto whole = m_index.constFind(key);
    if (whole != m_index.constEnd()) {
        return whole.value();
    }

    for (int taken = 1; taken <= 2 && taken < key.size(); ++taken) {
        if (!isPrefixLetter(key.at(taken - 1))) {
            break;
        }
        const QString rest = key.mid(taken);
        if (countsLetters(rest) < kMinimumForm) {
            break;
        }
        const auto found = m_index.constFind(rest);
        if (found != m_index.constEnd()) {
            if (peeled) {
                // From the folded key, so these are bare consonants. What they
                // get glued back onto is a pointed name, which is the same
                // small infelicity the abbreviation table already writes.
                *peeled = key.left(taken);
            }
            return found.value();
        }
    }

    return -1;
}

const NameGroup *NameForms::group(int index) const
{
    if (index < 0 || index >= m_groups.size()) {
        return nullptr;
    }
    return &m_groups.at(index);
}

} // namespace milah
