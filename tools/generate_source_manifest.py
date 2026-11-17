#!/usr/bin/env python3
# Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
# SPDX-License-Identifier: MIT

"""Generate the AMD source repository manifest for the configured Meson build."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


MEDIA_SUFFIXES = {
    ".avif",
    ".bmp",
    ".gif",
    ".ico",
    ".jpeg",
    ".jpg",
    ".mov",
    ".mp3",
    ".mp4",
    ".ogg",
    ".png",
    ".svg",
    ".tif",
    ".tiff",
    ".wav",
    ".webm",
    ".webp",
}


def run_json(command: list[str]) -> Any:
    result = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def git_output(repo: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify_file(path: str) -> str:
    suffix = Path(path).suffix.lower()
    if path.startswith(".github/"):
        return "continuous-integration"
    if path.startswith("subprojects/"):
        return "third-party-acquisition-metadata"
    if path.startswith("tests/"):
        return "test"
    if path.startswith("tools/"):
        return "development-tool"
    if path.startswith("include/") or path.startswith("src/"):
        return "amd-authored-source"
    if suffix in MEDIA_SUFFIXES:
        return "media"
    if path == "CMakeLists.txt" or path.startswith("cmake/"):
        return "build-metadata"
    if suffix in {".md", ".rst", ".txt"}:
        return "documentation-or-license"
    if path.startswith("meson") or suffix in {".build", ".ini", ".wrap"}:
        return "build-metadata"
    return "repository-metadata"


def normalize_source(path: str, repo: Path, build_dir: Path) -> str:
    source = Path(path).resolve()
    try:
        return source.relative_to(repo).as_posix()
    except ValueError:
        pass
    try:
        return f"<build>/{source.relative_to(build_dir).as_posix()}"
    except ValueError:
        return str(source)


def load_wrap(repo: Path, relative_path: str) -> dict[str, str]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(repo / relative_path)
    if "wrap-file" not in parser:
        raise ValueError(f"{relative_path} does not contain a [wrap-file] section")
    return dict(parser["wrap-file"])


def tracked_files(repo: Path, project_license: str) -> list[dict[str, Any]]:
    paths = sorted(
        path
        for path in git_output(
            repo, "ls-files", "--cached", "--others", "--exclude-standard"
        ).splitlines()
        if path
    )
    files = []
    for relative_path in paths:
        path = repo / relative_path
        if not path.is_file():
            continue
        files.append(
            {
                "path": relative_path,
                "classification": classify_file(relative_path),
                "size_bytes": path.stat().st_size,
                "sha256": sha256(path),
                "license": project_license,
            }
        )
    return files


def build_targets(
    raw_targets: list[dict[str, Any]],
    repo: Path,
    build_dir: Path,
    project_license: str,
) -> list[dict[str, Any]]:
    targets = []
    for target in raw_targets:
        sources: set[str] = set()
        for source_group in target.get("target_sources", []):
            for key in ("sources", "generated_sources"):
                sources.update(
                    normalize_source(path, repo, build_dir)
                    for path in source_group.get(key, [])
                )
        targets.append(
            {
                "name": target["name"],
                "type": target["type"],
                "output_files": sorted(Path(path).name for path in target["filename"]),
                "installed": target.get("installed", False),
                "source_files": sorted(sources),
                "license": project_license,
            }
        )
    return sorted(targets, key=lambda target: (target["name"], target["type"]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--metadata",
        type=Path,
        default=Path("manifest/manifest-metadata.json"),
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    build_dir = args.build_dir.resolve()
    metadata_path = args.metadata
    if not metadata_path.is_absolute():
        metadata_path = repo / metadata_path
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))

    project_info = run_json(["meson", "introspect", str(build_dir), "--projectinfo"])
    raw_targets = run_json(["meson", "introspect", str(build_dir), "--targets"])
    project_license = metadata["project"]["license"]

    resolved_subprojects = {
        item["name"]: item.get("version")
        for item in project_info.get("subprojects", [])
    }
    third_party = []
    for component in metadata["third_party_components"]:
        wrap = load_wrap(repo, component["wrap_file"])
        meson_name = component["meson_subproject"]
        version = resolved_subprojects.get(meson_name)
        if not version:
            raise ValueError(f"Meson did not resolve third-party component {meson_name}")
        expected_directory = wrap.get("directory", "")
        if version not in expected_directory:
            raise ValueError(
                f"Resolved {meson_name} version {version} does not match {expected_directory}"
            )
        third_party.append(
            {
                "name": component["name"],
                "version": version,
                "use": component["use"],
                "license": component["license"],
                "copyright": component["copyright"],
                "upstream_license_file": component["upstream_license_file"],
                "original_location": wrap["source_url"],
                "file_name": wrap["source_filename"],
                "sha256": wrap["source_hash"],
                "fallback_location": wrap.get("source_fallback_url"),
                "dynamically_pulled": True,
                "acquisition_metadata": component["wrap_file"],
            }
        )

    files = tracked_files(repo, project_license)
    tracked_paths = {item["path"] for item in files}
    for asset in metadata["media_assets"]:
        missing = {"path", "origin", "license"} - asset.keys()
        if missing:
            raise ValueError(
                f"Media asset metadata is missing {sorted(missing)}: {asset}"
            )
    detected_media = sorted(
        item["path"] for item in files if item["classification"] == "media"
    )
    declared_media = sorted(item["path"] for item in metadata["media_assets"])
    if detected_media != declared_media:
        raise ValueError(
            "Media metadata is incomplete: "
            f"detected={detected_media}, declared={declared_media}"
        )

    for exclusion in metadata["internal_only_paths"]:
        missing = {"path", "reason"} - exclusion.keys()
        if missing:
            raise ValueError(
                f"Internal-only path metadata is missing {sorted(missing)}: "
                f"{exclusion}"
            )
        excluded_path = exclusion["path"].rstrip("/")
        if not any(
            path == excluded_path or path.startswith(f"{excluded_path}/")
            for path in tracked_paths
        ):
            raise ValueError(f"Internal-only path is not tracked: {excluded_path}")

    manifest = {
        "schema": "amd-source-repository-manifest-v1",
        "application": {
            "name": project_info["descriptive_name"],
            "version": project_info["version"],
            "repository": metadata["project"]["repository"],
            "revision": git_output(repo, "rev-parse", "HEAD").strip(),
            "license": project_license,
            "license_file": metadata["project"]["license_file"],
        },
        "third_party_components": third_party,
        "dependencies": third_party,
        "binary_targets": build_targets(
            raw_targets, repo, build_dir, project_license
        ),
        "media_assets": metadata["media_assets"],
        "internal_only_paths_not_required_for_release": metadata[
            "internal_only_paths"
        ],
        "repository_files": files,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
