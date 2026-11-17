#!/usr/bin/env python3
"""Run the isolated libCPER replacement gate against a frozen ADDC replay."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--normalizer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    totals = {"inputs": 0, "libcper_ok": 0, "normalizer_ok": 0, "exact": 0}
    samples: list[dict[str, object]] = []
    with args.baseline.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            totals["inputs"] += 1
            converted = subprocess.run(
                [str(args.converter), "to-json", str(args.workspace / record["path"])],
                capture_output=True,
                text=True,
            )
            if converted.returncode != 0:
                if len(samples) < 10:
                    samples.append({"path": record["path"], "stage": "libcper",
                                    "returncode": converted.returncode})
                continue
            totals["libcper_ok"] += 1
            normalized = subprocess.run(
                [str(args.normalizer)], input=converted.stdout,
                capture_output=True, text=True,
            )
            if normalized.returncode != 0:
                if len(samples) < 10:
                    samples.append({"path": record["path"], "stage": "normalizer"})
                continue
            totals["normalizer_ok"] += 1
            candidate = json.loads(normalized.stdout)
            if candidate == record["parser"].get("result"):
                totals["exact"] += 1
            elif len(samples) < 10:
                samples.append({"path": record["path"], "stage": "comparison"})

    report = {
        "libcper_revision": "7166cb30a3847b07d218089e3929c5734eae4953",
        "gate": "reject" if totals["exact"] != totals["inputs"] else "accept",
        "totals": totals,
        "sample_failures": samples,
        "production_parser_changed": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
