#include "core/word_marker.h"

#include "core/lexicon.h"
#include "core/suggestions.h"

namespace milah {

WordMarker markerFor(const QString &word, const UserDictionary &dictionary)
{
    WordMarker marker;
    if (word.isEmpty()) {
        return marker;
    }

    // What the editor has written about this word themselves, which is worth
    // reading whether or not the Hebrew Bible has heard of it.
    const QStringList defined = dictionary.definitionsFor(word);

    const QList<LexiconEntry> entries = HebrewLexicon::shared().lookup(word);
    if (entries.isEmpty()) {
        // Strong's covers the Hebrew Bible only, so a post-biblical word has no
        // number and never will. Saying which kind of absence this is keeps the
        // row from reading as though the word were doubtful.
        if (AttestedForms::shared().contains(word)) {
            marker.text = QStringLiteral("M");
            marker.tooltip = QStringLiteral("%1 is attested in the Mishnah or Tosefta. "
                                            "Strong's covers only the Hebrew Bible, so "
                                            "there is no number for it.")
                                 .arg(word);
        } else if (!defined.isEmpty()) {
            // A dash says nothing knows this word, which stops being true the
            // moment the editor defines it. So D stands alone here rather than
            // riding on a dash it contradicts.
            marker.text = QStringLiteral("D");
        } else {
            marker.text = QStringLiteral("—");
            marker.tooltip = QStringLiteral("%1 is not attested in the Hebrew Bible, the "
                                            "Mishnah or the Tosefta.")
                                 .arg(word);
        }
    } else {
        // Several words can share a consonantal skeleton, so the likeliest
        // reading is shown with a mark and the alternatives kept in reach
        // rather than one of them being passed off as the answer.
        marker.text = entries.size() > 1
            ? QStringLiteral("%1?").arg(entries.first().strongs)
            : entries.first().strongs;
        marker.tooltip = strongsTooltip(entries);
    }

    // A word can be both attested and worth a note of one's own. What the
    // corpora say keeps the cell — it is the harder fact — and the note rides
    // beside it. The one exception is handled above: D never follows a dash.
    if (!defined.isEmpty() && marker.text != QStringLiteral("D")) {
        marker.text += QStringLiteral("·D");
    }
    if (!defined.isEmpty()) {
        marker.tooltip = marker.tooltip.isEmpty()
            ? QStringLiteral("%1\n\nYour own definition.").arg(numberedDefinitions(defined))
            : QStringLiteral("Your own definition:\n%1\n\n%2")
                  .arg(numberedDefinitions(defined), marker.tooltip);
    }

    return marker;
}

QString markerRowTooltip()
{
    return QStringLiteral(
        "Strong's numbers for the words above, from the Hebrew Bible.\n"
        "M is attested in the Mishnah or Tosefta; a dash is attested in "
        "neither — not that the word is wrong.\n"
        "·D marks a word you have defined yourself, and rides beside whatever "
        "else is known about it. D alone is a word only you have defined. "
        "Either way the meaning is in the tooltip.");
}

} // namespace milah
