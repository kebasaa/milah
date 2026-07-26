import os
import re
from pypdf import PdfReader
from xml.sax.saxutils import escape as xml_escape

MS_LINE_THRESHOLD = 100  # heuristic: extracted verse numbers > this are treated as manuscript line numbers


def collect_global_footnotes(content):
    footnotes = {}
    current_num = None
    current_text = ''

    footnote_delimiters = [
        'Hebrew Transcription',
        'The Scriptures:',
        'Aramaic:',
        'Interlinear Chart',
        '© copyright',
        'Page ',
    ]

    lines = content.split('\n')
    for line in lines:
        line = line.strip()
        if not line or line.startswith("Translation:") or line.startswith("Revelation") or line.startswith("Hebrew Transcription") or line.startswith("The Scriptures") or line.startswith("Aramaic") or line.startswith("Interlinear Chart"):
            continue

        # Skip page numbers like "X of 380"
        if re.match(r'^\d+ of \d+$', line):
            continue

        match = re.match(r'^\s*(\d+)\s+(.*)', line)
        if match:
            if current_num is not None:
                footnotes[current_num] = current_text.strip()

            num_str = match.group(1)
            text = match.group(2).lstrip()

            parts = text.split(' ')
            while parts and parts[0].isdigit():
                num_str += parts.pop(0)
                text = ' '.join(parts)

            try:
                current_num = int(num_str)
            except ValueError:
                current_num = None
            current_text = text
        elif current_num is not None:
            is_delimiter = any(line.startswith(d) for d in footnote_delimiters)
            if is_delimiter:
                if current_num is not None:
                    footnotes[current_num] = current_text.strip()
                current_num = None
                current_text = ''
            elif line:
                current_text += ' ' + line
    if current_num is not None:
        footnotes[current_num] = current_text.strip()

    return footnotes


def parse_hebrew(content, expected_chapter_num):
    """
    Line-oriented parser (tolerant header matching).

    Returns list of entries:
      [ [verse_num, raw_segment_text, hebrew_text, alt_ref_or_None, ms_line_or_None], ... ]
    """

    # Header: capture chap:verse and optional parenthetical; allow any trailing characters
    header_re = re.compile(
        r'^[ \t]*Revelation\s+(\d+):(\d{1,4})(?:\s*\(\s*(.*?)\s*\))?.*$',
        re.I | re.M
    )

    cochin_inner_re = re.compile(r'(?i)Cochin\s+(\d+):(\d{1,3})')
    absent_re = re.compile(
        r'(?i)\b(does not exist|doesn\'t exist|not (?:present|in the Cochin|in this manuscript)|absent|missing|omitted|no verse here|omitted in Cochin)\b'
    )

    lines = content.splitlines()

    # Find headers: list of tuples (line_index, matchobj)
    headers = []
    for idx, ln in enumerate(lines):
        m = header_re.match(ln)
        if m:
            chap = int(m.group(1))
            if chap == expected_chapter_num:
                headers.append((idx, m))

    if not headers:
        return []

    entries = []

    for h_i, (line_idx, match) in enumerate(headers):
        raw_verse_num = int(match.group(2))
        inner = match.group(3)  # parenthetical inner text if present

        alt_ref = None
        explicitly_absent = False

        header_line = lines[line_idx]  # full original header line

        # Prefer parenthetical, but if missing, also scan the full header line
        inner_text_to_search = inner.strip() if inner and inner.strip() else None

        if inner_text_to_search:
            cm = cochin_inner_re.search(inner_text_to_search)
            if cm:
                alt_ref = (int(cm.group(1)), int(cm.group(2)))
            elif absent_re.search(inner_text_to_search):
                explicitly_absent = True
        else:
            cm = cochin_inner_re.search(header_line)
            if cm:
                alt_ref = (int(cm.group(1)), int(cm.group(2)))
            elif absent_re.search(header_line):
                explicitly_absent = True

        # determine segment lines: from next line after header up to next header line (exclusive)
        seg_start_line = line_idx + 1
        seg_end_line = headers[h_i + 1][0] if (h_i + 1) < len(headers) else len(lines)
        segment_lines = lines[seg_start_line:seg_end_line]
        segment_text = "\n".join(segment_lines)

        hebrew_text = ""
        ms_line = None

        # If explicitly marked absent -> leave hebrew_text empty
        if not explicitly_absent:
            # Find a line in segment that starts with Aramaic:
            ar_idx = None
            for offset, l in enumerate(segment_lines):
                if re.match(r'^[ \t]*Aramaic:', l, re.I):
                    ar_idx = offset
                    break

            if ar_idx is not None:
                # Search for Interlinear line AFTER ar_idx within this same segment
                inter_idx = None
                for offset in range(ar_idx + 1, len(segment_lines)):
                    if re.match(r'^[ \t]*Interlinear\b', segment_lines[offset], re.I):
                        inter_idx = offset
                        break

                # Only extract if interlinear exists within the same verse segment
                if inter_idx is not None and inter_idx > ar_idx:
                    block_lines = segment_lines[ar_idx + 1:inter_idx]
                    hebrew_re = re.compile(r'[\u0590-\u05FF]')
                    ascii_re = re.compile(r'[A-Za-z]')
                    kept = []
                    for bl in block_lines:
                        bls = bl.strip()
                        if not bls:
                            continue
                        if not hebrew_re.search(bls):
                            continue
                        if ascii_re.search(bls):
                            continue
                        kept.append(bls)
                    hebrew_text = " ".join(kept)
                    hebrew_text = re.sub(r'\d+', '', hebrew_text)
                    hebrew_text = re.sub(r'\s{2,}', ' ', hebrew_text).strip()
                else:
                    # No interlinear inside this verse segment => treat as intentionally empty
                    hebrew_text = ""
            else:
                # No Aramaic in segment => empty
                hebrew_text = ""

        # Detect manuscript-line numbering (heuristic)
        if raw_verse_num > MS_LINE_THRESHOLD:
            # treat raw_verse_num as manuscript line number, don't use as canonical verse num
            ms_line = raw_verse_num
            canonical_num = None  # will be assigned later in sequential pass
        else:
            canonical_num = raw_verse_num

        entries.append([canonical_num, segment_text, hebrew_text, alt_ref, ms_line])

    # If we found any ms_line values (i.e. parsed numbers looked like manuscript lines),
    # reindex canonical verse numbers sequentially starting at 1 for the chapter
    if any(e[4] is not None for e in entries):
        print(f"Note: detected manuscript-style line numbers (> {MS_LINE_THRESHOLD}) in chapter {expected_chapter_num}. Reindexing canonical verse numbers sequentially and preserving manuscript lines in ms_line attribute.")
        for i, e in enumerate(entries):
            e[0] = i + 1  # canonical verse number 1..n

    # Diagnostic: do not modify verse numbers, but warn if not strictly increasing
    nums = [e[0] for e in entries]
    if len(nums) > 1 and not all(nums[i] < nums[i + 1] for i in range(len(nums) - 1)):
        print("Warning: verse numbers in chapter", expected_chapter_num,
              "are not strictly increasing. Extracted verse numbers (in order):", nums)

    return entries


def combine_all_chapters_to_osis(all_chapters_data, header_path, output_path):
    """
    Build a single OSIS file from all_chapters_data.

    all_chapters_data: list of (chapter_num, verse_entries)
      verse_entries: list of [verse_num, raw_segment, hebrew_text, alt_ref_or_None, ms_line_or_None]
    """

    from collections import Counter, defaultdict
    import io

    # helper: index -> alpha letters (0 -> 'a', 25 -> 'z', 26 -> 'aa', etc.)
    def index_to_letters(idx):
        if idx < 0:
            raise ValueError("index must be non-negative")
        letters = ''
        n = idx + 1
        while n > 0:
            n -= 1
            letters = chr(ord('a') + (n % 26)) + letters
            n //= 26
        return letters

    # Ensure chapters sorted by numeric chapter number
    all_chapters_data.sort(key=lambda x: x[0])

    buf = io.StringIO()
    buf.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    # Declare alt and ms namespaces so alt:num and ms:line are legal
    buf.write('<osis xmlns="http://www.bibletechnologies.net/2003/OSIS/namespace"\n')
    buf.write('      xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"\n')
    buf.write('      xmlns:alt="https://projecttruthministries.org/studies/cochin-revelation/"\n')
    buf.write('      xmlns:ms="https://projecttruthministries.org/studies/cochin-revelation/"\n')
    buf.write('      xsi:schemaLocation="http://www.bibletechnologies.net/2003/OSIS/namespace\n')
    buf.write('                          http://www.bibletechnologies.net/osisCore.2.1.1.xsd">\n')

    # osisText attributes (these values are used in the header below)
    osis_id_work = "MS.Oo.1.16.2_REV_Hebrew"
    osis_ref_work = "bible"
    buf.write(f'<osisText osisIDWork="{osis_id_work}"\n')
    buf.write(f'          osisRefWork="{osis_ref_work}"\n')
    buf.write('          xml:lang="he">\n')

    # Write a header block that at minimum declares the two works referenced above.
    buf.write('  <header>\n')
    # Minimal work entry for osisIDWork
    buf.write(f'    <work osisWork="{osis_id_work}">\n')
    buf.write('      <title>Revelation (Cochin MS Oo.1.16.2)</title>\n')
    buf.write(f'      <identifier type="OSIS">{osis_id_work}</identifier>\n')
    buf.write('      <refSystem>MS.Oo.1.16.2</refSystem>\n')
    buf.write('      <language>he</language>\n')
    buf.write('    </work>\n')

    # Minimal work entry for reference system
    buf.write(f'    <work osisWork="{osis_ref_work}">\n')
    buf.write('      <title>Referenced versification (standard)</title>\n')
    buf.write(f'      <identifier type="OSIS">{osis_ref_work}</identifier>\n')
    buf.write('      <refSystem>StandardV11N</refSystem>\n')
    buf.write('      <language>he</language>\n')
    buf.write('    </work>\n')

    # If header file exists, insert its contents (but strip an outer <header> wrapper if present)
    try:
        with open(header_path, 'r', encoding='utf-8') as hf:
            header = hf.read().rstrip()
        if header:
            # Remove outer <header> ... </header> if present to avoid nested headers
            header_inner = re.sub(r'^\s*<header[^>]*>\s*', '', header, flags=re.I)
            header_inner = re.sub(r'\s*</header>\s*$', '', header_inner, flags=re.I)
            # Indent header contents by 4 spaces for readability
            for line in header_inner.splitlines():
                buf.write('    ' + line + '\n')
    except FileNotFoundError:
        print(f"Warning: header file not found: {header_path} -- writing minimal header only")

    buf.write('  </header>\n')

    # Start book wrapper (assumes all chapters belong to book 'Rev')
    buf.write('  <div type="book" osisID="Rev">\n')

    # For each chapter, compute per-chapter duplicate verse counts and assign suffixes when needed
    for chapter_num, verse_entries in all_chapters_data:
        buf.write(f'    <chapter osisID="Rev.{chapter_num}">\n')

        # Compute counts for verse numbers in this chapter
        verse_nums = [ve[0] for ve in verse_entries]
        counts_total = Counter(verse_nums)
        occurrence_index = defaultdict(int)

        # Write each verse line
        for verse_num, raw_segment, hebrew_text, alt_ref, ms_line in verse_entries:
            if counts_total[verse_num] > 1:
                occ = occurrence_index[verse_num]
                suffix = index_to_letters(occ)
                occurrence_index[verse_num] += 1
                osis_id = f"Rev.{chapter_num}.{verse_num}{suffix}"
                n_attr = f' n="{verse_num}{suffix}"'
            else:
                osis_id = f"Rev.{chapter_num}.{verse_num}"
                n_attr = f' n="{verse_num}"'

            alt_attr = ""
            if alt_ref:
                alt_chap, alt_verse = alt_ref
                alt_attr = f' alt:num="Rev.{alt_chap}.{alt_verse}"'

            ms_attr = ""
            if ms_line is not None:
                ms_attr = f' ms:line="{ms_line}"'

            # Escape text to be well-formed XML
            verse_text_escaped = xml_escape(hebrew_text if hebrew_text is not None else "")

            # If the raw segment contains other useful markup or notes you want preserved,
            # you may add them in a <note> element. Currently we output only the hebrew text.
            buf.write(f'      <verse osisID="{osis_id}"{n_attr}{alt_attr}{ms_attr}>{verse_text_escaped}</verse>\n')

        buf.write('    </chapter>\n')

    # Close book div and OSIS envelopes
    buf.write('  </div>\n')
    buf.write('</osisText>\n')
    buf.write('</osis>\n')

    # Write output file
    with open(output_path, 'w', encoding='utf-8') as out_f:
        out_f.write(buf.getvalue())


# Main process
pdf_path = "original_cochin/The-Scroll-Of-The-Mysteries_PUBLISHING-BOOK_2nd-Edition_10-18-2025_OTHER.pdf"
output_osis_path = "osis/Cochin_MS_Oo.1.16.2_REV_hebrew.osis"
header_path = "headers/cochin_rev_transcriptions.txt"

if not os.path.exists(os.path.dirname(output_osis_path)):
    os.makedirs(os.path.dirname(output_osis_path))

print(f"Processing {pdf_path}...")
# Extract text
with open(pdf_path, 'rb') as f:
    reader = PdfReader(f)
    full_pdf_text = ''
    for page in reader.pages:
        text = page.extract_text()
        if text:
            full_pdf_text += text + '\n'

all_chapters_data = []  # To store (chapter_num, hebrew_verses) for all chapters

# Find all chapter markers
chapter_markers = list(re.finditer(r'Cochin Revelation Chapter (\d+)', full_pdf_text))

if not chapter_markers:
    print("No chapter markers found. Cannot process.")
else:
    for i, marker in enumerate(chapter_markers):
        current_chapter_num = int(marker.group(1))
        start_pos = marker.start()
        end_pos = chapter_markers[i + 1].start() if i + 1 < len(chapter_markers) else len(full_pdf_text)

        chapter_segment_text = full_pdf_text[start_pos:end_pos]

        print(f"Processing Chapter {current_chapter_num}...")
        # Parse the chapter segment
        hebrew_verses = parse_hebrew(chapter_segment_text, current_chapter_num)

        if hebrew_verses:
            all_chapters_data.append((current_chapter_num, hebrew_verses))
        else:
            print(f"No Hebrew verses extracted for Chapter {current_chapter_num}.")

# Now, generate the combined OSIS file
if all_chapters_data:
    combine_all_chapters_to_osis(all_chapters_data, header_path, output_osis_path)
    print(f"Final combined OSIS file written to: {output_osis_path}")
else:
    print("No chapters were extracted. Final OSIS file not written.")

print("Done.")
