#include "core/transcription_osis.h"

#include "core/alignment.h"

namespace milah {

TranscriptionDrafts transcriptionDrafts(const TranscriptionDocument &original)
{
    TranscriptionDrafts out;

    // The marginalia come out of the running text and go in as notes on the
    // lines they stand beside: they are on the leaf, but they are not words of
    // any verse of the work.
    const TranscriptionDocument document = withoutMarginalia(original);
    for (const TranscribedPage &page : document.pages) {
        for (int index = 0; index < page.verses.size(); ++index) {
            const TranscribedVerse &verse = page.verses.at(index);
            const QString id = transcribedVerseId(page, index);
            if (id.isEmpty()) {
                ++out.unnamed;
                for (const TranscribedWord &word : verse.words) {
                    if (!word.note.isEmpty()) {
                        // A note is anchored to a verse by its osisID, and this
                        // verse has none — so it goes nowhere, and that is
                        // worth saying rather than discovering later.
                        ++out.strandedNotes;
                    }
                }
                continue;
            }

            // A verse already begun on an earlier folio carries on where it
            // left off, rather than replacing what that folio held.
            const bool continuing = out.drafts.contains(id);
            CombinedDraft &draft = out.drafts[id];
            if (!continuing) {
                draft.reference.id = id;
                draft.reference.book = page.book;
                draft.reference.chapter = chapterOfVerse(page, index);
                draft.reference.verse = verse.number;
            }
            const int first = int(draft.columns.size());

            QList<int> notedColumns;
            for (int word = 0; word < verse.words.size(); ++word) {
                const TranscribedWord &written = verse.words.at(word);
                const int column = first + word;
                ConsensusColumn cell;
                cell.text = written.hebrew;
                draft.columns.append(cell);
                if (!written.english.isEmpty()) {
                    out.glosses[id].insert(column, written.english);
                }
                if (!written.note.isEmpty()) {
                    notedColumns.append(column);
                }
            }

            // Anchored twice, because the two writers ask different questions.
            // The interlinear one writes each word separately and wants to know
            // which word; the plain one writes running text and wants to know
            // how many characters in. Answering only one of them is how every
            // note in a verse ends up piled onto its first word.
            for (const int column : notedColumns) {
                SourceNote note;
                note.text = verse.words.at(column - first).note;
                note.tokenIndex = column;
                note.charOffset = columnCharOffset(draft, column);
                note.number = QString::number(out.apparatus.notes[id].size() + 1);
                out.apparatus.notes[id].append(note);
            }
        }
    }

    return out;
}

} // namespace milah
