import os
import sys
from pypdf import PdfReader

# Main process
if len(sys.argv) > 1:
    pdf_path = sys.argv[1]
else:
    print("Usage: python 00_extract_raw.py <pdf_path>")
    sys.exit(1)

txt_path = os.path.splitext(pdf_path)[0] + '.txt'

print("Extracting text from PDF...")
with open(pdf_path, 'rb') as f:
    reader = PdfReader(f)
    text = ''
    for page in reader.pages:
        text += page.extract_text() + '\n'

with open(txt_path, 'w', encoding='utf-8') as f:
    f.write(text)

print("Done. Text saved.")
