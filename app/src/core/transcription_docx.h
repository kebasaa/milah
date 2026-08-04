#pragma once

#include "core/docx.h"
#include "core/transcription.h"

namespace milah {

/// The transcription as a text to read: the Hebrew running right to left, verse
/// numbers superscripted where they fall, a heading at each chapter, and the
/// transcriber's remarks as footnotes at the foot of the page.
DocxDocument readingWordDocument(const TranscriptionDocument &transcription);

/// The transcription verse by verse: each verse of Hebrew on its own line, with
/// the transcriber's English underneath it.
///
/// The English is their glosses in the order the Hebrew words stand, so the
/// nth word of the line below is the nth word of the line above. That
/// correspondence is the whole of what makes it an interlinear.
DocxDocument interlinearWordDocument(const TranscriptionDocument &transcription);

/// Verses carrying text that no heading could name — no book, no chapter, no
/// number. Counted rather than refused: unlike OSIS, a Word document can print
/// a verse nobody has numbered yet, and the count is what the status line says.
int unnamedVerseCount(const TranscriptionDocument &transcription);

} // namespace milah
