from __future__ import annotations

import argparse
from dataclasses import asdict
import json
from pathlib import Path
import sys

from .compare import compare_directories
from .converter import ConversionError, ConversionReport, convert_pdf
from .profiles import BOOK_PROFILES, get_profile


def _report(report: ConversionReport) -> None:
    data = asdict(report)
    data["input_path"] = str(report.input_path)
    data["output_paths"] = {
        key: str(value)
        for key, value in report.output_paths.items()
    }
    print(json.dumps(data, ensure_ascii=False, indent=2))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pdf2osis",
        description="Convert the Cochin Revelation and James PDFs to OSIS.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    convert = subparsers.add_parser("convert", help="convert one PDF")
    convert.add_argument("--book", choices=sorted(BOOK_PROFILES), required=True)
    convert.add_argument("--input", type=Path, required=True)
    convert.add_argument("--output-dir", type=Path, required=True)

    convert_all = subparsers.add_parser(
        "convert-all",
        help="convert the canonical Revelation and James PDFs",
    )
    convert_all.add_argument("--source-dir", type=Path, required=True)
    convert_all.add_argument("--output-dir", type=Path, required=True)

    compare = subparsers.add_parser(
        "compare",
        help="audit generated OSIS against historical reference files",
    )
    compare.add_argument("--book", choices=sorted(BOOK_PROFILES), required=True)
    compare.add_argument("--generated-dir", type=Path, required=True)
    compare.add_argument("--reference-dir", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "convert":
            report = convert_pdf(
                args.input,
                get_profile(args.book),
                args.output_dir,
            )
            _report(report)
        elif args.command == "convert-all":
            for profile in BOOK_PROFILES.values():
                report = convert_pdf(
                    profile.default_path(args.source_dir),
                    profile,
                    args.output_dir,
                )
                _report(report)
        else:
            report = compare_directories(
                args.generated_dir,
                args.reference_dir,
                get_profile(args.book),
            )
            print(
                json.dumps(
                    report.to_dict(),
                    ensure_ascii=False,
                    indent=2,
                )
            )
            if report.generated_regressions:
                return 1
    except (ConversionError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0
