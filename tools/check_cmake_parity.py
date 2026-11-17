#!/usr/bin/env python3
# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

"""Check that the CMake build mirrors the Meson build.

Compares two configured build directories (same product selection) for:
  * the multiset of compiled sources and the ADDC_* defines each is built with,
  * the registered test names,
  * if both are built, the API symbols exported by libaddc_lib.
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from collections import Counter
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
# Mangled names in namespace std (_ZSt..., _ZNSt..., _ZNKSt...).
STD_SYMBOL = re.compile(r"_Z(N[KVRO]*)?St")


def compile_units(build_dir: Path) -> Counter[tuple[str, tuple[str, ...]]]:
    units: Counter[tuple[str, tuple[str, ...]]] = Counter()
    for entry in json.loads((build_dir / "compile_commands.json").read_text()):
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        try:
            source_name = source.relative_to(REPO).as_posix()
        except ValueError:
            continue
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        defines = tuple(
            sorted(arg[2:] for arg in arguments if arg.startswith("-DADDC_"))
        )
        units[(source_name, defines)] += 1
    return units


def meson_tests(build_dir: Path) -> set[str]:
    result = subprocess.run(
        ["meson", "introspect", str(build_dir), "--tests"],
        check=True, capture_output=True, text=True,
    )
    return {test["name"] for test in json.loads(result.stdout)}


def cmake_tests(build_dir: Path) -> set[str]:
    result = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
        check=True, capture_output=True, text=True,
    )
    return {test["name"] for test in json.loads(result.stdout).get("tests", [])}


def api_symbols(library: Path) -> set[str]:
    result = subprocess.run(
        ["nm", "-D", "--defined-only", str(library)],
        check=True, capture_output=True, text=True,
    )
    # Leaked libstdc++ template instantiations vary with optimization flags.
    symbols = (line.split()[-1] for line in result.stdout.splitlines() if line.strip())
    return {symbol for symbol in symbols if not STD_SYMBOL.match(symbol)}


def report(label: str, meson_only: list, cmake_only: list) -> bool:
    if not meson_only and not cmake_only:
        return True
    print(f"{label} differ:", file=sys.stderr)
    for item in meson_only:
        print(f"  only in Meson: {item}", file=sys.stderr)
    for item in cmake_only:
        print(f"  only in CMake: {item}", file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--meson-build-dir", required=True, type=Path)
    parser.add_argument("--cmake-build-dir", required=True, type=Path)
    args = parser.parse_args()

    ok = True
    meson_units = compile_units(args.meson_build_dir)
    cmake_units = compile_units(args.cmake_build_dir)
    ok &= report(
        "Compiled sources/defines",
        sorted((meson_units - cmake_units).elements()),
        sorted((cmake_units - meson_units).elements()),
    )

    mt, ct = meson_tests(args.meson_build_dir), cmake_tests(args.cmake_build_dir)
    ok &= report("Tests", sorted(mt - ct), sorted(ct - mt))

    meson_lib = args.meson_build_dir / "libaddc_lib.so"
    cmake_lib = args.cmake_build_dir / "libaddc_lib.so"
    if meson_lib.exists() and cmake_lib.exists():
        ms, cs = api_symbols(meson_lib), api_symbols(cmake_lib)
        ok &= report("Exported API symbols", sorted(ms - cs), sorted(cs - ms))

    if ok:
        print(f"CMake matches Meson: {sum(meson_units.values())} compile units, "
              f"{len(mt)} tests")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
