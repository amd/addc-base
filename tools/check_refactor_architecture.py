#!/usr/bin/env python3
"""Reject forbidden ADDC architecture patterns when run with --strict."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess


RULES = {
    "dynamic_cast": re.compile(r"\bdynamic_cast\s*<"),
    "decoder_registry": re.compile(
        r"\b(?:DecoderRegistry|McaDecoderRegistry|SectionRegistry)\b"
    ),
    "decoder_factory": re.compile(
        r"\b(?:registry_factory|build_[a-z0-9_]*registry)\b", re.I
    ),
    "numeric_priority": re.compile(r"\bpriority\s*(?:\{|\(|=|:)"),
    "tier_decoder_inheritance": re.compile(
        r"\bclass\s+\w+(?:Extended|Full)\w*\s*:\s*public\s+[^\n{]*Decoder"
    ),
}


def tracked(root: Path) -> list[Path]:
    completed = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True,
        capture_output=True,
    )
    return [root / item.decode() for item in completed.stdout.split(b"\0") if item]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    counts: Counter[str] = Counter()
    findings: list[dict[str, object]] = []
    for path in tracked(root):
        relative = path.relative_to(root).as_posix()
        if not relative.startswith(("include/", "src/")) or not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        for line_number, line in enumerate(
            data.decode("utf-8", errors="replace").splitlines(), start=1
        ):
            for rule, pattern in RULES.items():
                if pattern.search(line):
                    counts[rule] += 1
                    findings.append(
                        {"rule": rule, "path": relative, "line": line_number}
                    )

    report = {
        "mode": "strict" if args.strict else "report-only",
        "counts": dict(sorted(counts.items())),
        "total": sum(counts.values()),
        "findings": findings,
    }
    summary = {key: value for key, value in report.items() if key != "findings"}
    print(json.dumps(summary, sort_keys=True))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 1 if args.strict and findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
