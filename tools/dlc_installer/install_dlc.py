#!/usr/bin/env python3
"""Install user-owned game content as deterministic loose GHOGX DLC.

The source media is read-only. GH2 is validated as the base game and is never
rewritten. Every imported payload is staged below the destination DLC root,
audited, conflict-checked, and then installed by an atomic directory rename.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from setup_runtime import python_tool_command, resource_root


SCHEMA_VERSION = 1
CREATE_NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0
PS2_DISC_PROFILES = {
    "gh1": {"disc_ids": {"SLUS-21224"}, "system_versions": {"1.00"}},
    "gh2": {"disc_ids": {"SLUS-21447"}, "system_versions": {"1.00"}},
    "gh80s": {"disc_ids": {"SLUS-21586"}, "system_versions": {"1.00"}},
}
PACKAGE_NAMES = {
    "gh1": ("disc.gh1.songs", "Guitar Hero Songs"),
    "gh80s": ("disc.gh80s.songs", "Guitar Hero Encore: Rocks the 80s Songs"),
}
RB2_WII_DISC_ID = "SZAE69"
RB2_WII_DISC_REVISIONS = {0}


class InstallError(RuntimeError):
    pass


def emit_progress(percent: int, status: str) -> None:
    print(f"GHC_SETUP_PROGRESS {percent} {status}", flush=True)


def installer_helper(name: str) -> Path:
    candidates = (
        resource_root() / "tools/dlc_installer" / name,
        Path(__file__).resolve().with_name(name),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise InstallError(f"Embedded setup helper is missing: {name}")


@dataclass(frozen=True)
class ArkSource:
    role: str
    source: Path
    disc_id: str
    system_version: str
    hdr: Path
    arks: tuple[Path, ...]
    source_sha256: str
    extracted_from_image: bool
    intro_video: Path | None = None


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_fingerprint(paths: Iterable[Path]) -> str:
    resolved = sorted((path.resolve() for path in paths), key=str)
    if not resolved:
        return hashlib.sha256(b"").hexdigest()
    common = Path(os.path.commonpath([str(path.parent) for path in resolved]))
    digest = hashlib.sha256()
    for path in resolved:
        digest.update(path.relative_to(common).as_posix().casefold().encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".{uuid.uuid4().hex}.tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def read_cache_marker(path: Path, expected: dict[str, Any]) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return None
    if not isinstance(value, dict):
        return None
    if any(value.get(key) != expected_value
           for key, expected_value in expected.items()):
        return None
    return value


def record_cache_event(
    events: list[dict[str, Any]] | None,
    cache: Path,
    status: str,
    source_sha256: str,
) -> None:
    if events is not None:
        events.append(
            {
                "cache": str(cache.resolve()),
                "status": status,
                "source_sha256": source_sha256,
                "recorded_utc": utc_now(),
            }
        )


def remove_tree(path: Path, ignore_errors: bool = False) -> None:
    """Remove installer-owned trees, including deep Windows ARK paths."""
    path = path.resolve()
    target = path
    if os.name == "nt" and not str(path).startswith("\\\\?\\"):
        target = Path("\\\\?\\" + str(path))
    try:
        shutil.rmtree(target)
    except FileNotFoundError:
        return
    except OSError:
        if ignore_errors:
            return
        raise


def windows_extended_path(path: Path) -> Path:
    resolved = path.resolve()
    if os.name == "nt" and not str(resolved).startswith("\\\\?\\"):
        return Path("\\\\?\\" + str(resolved))
    return resolved


def copy_tree(source: Path, destination: Path) -> None:
    """Copy a complete tree without the legacy Windows MAX_PATH limit."""
    shutil.copytree(
        windows_extended_path(source),
        windows_extended_path(destination),
    )


def run(
    command: list[str],
    journal: list[dict[str, Any]],
    cwd: Path | None = None,
    path_prepend: Path | None = None,
) -> str:
    started = utc_now()
    environment = None
    if path_prepend is not None:
        environment = os.environ.copy()
        environment["PATH"] = str(path_prepend.resolve()) + os.pathsep + environment.get("PATH", "")
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        creationflags=CREATE_NO_WINDOW,
    )
    journal.append(
        {
            "argv": command,
            "cwd": str(cwd.resolve()) if cwd else None,
            "path_prepend": str(path_prepend.resolve()) if path_prepend else None,
            "started_utc": started,
            "completed_utc": utc_now(),
            "exit_code": result.returncode,
            "output_tail": result.stdout[-8000:],
        }
    )
    if result.returncode:
        raise InstallError(
            f"command failed ({result.returncode}): "
            f"{subprocess.list2cmdline(command)}\n{result.stdout[-2000:]}"
        )
    return result.stdout


def normalized_disc_id(system_cnf: str) -> str:
    match = re.search(
        r"BOOT2?\s*=\s*cdrom0:\\\\?([A-Z]{4})[_-](\d{3})[.]?(\d{2})",
        system_cnf,
        flags=re.IGNORECASE,
    )
    if not match:
        raise InstallError("SYSTEM.CNF does not contain a recognized PS2 BOOT entry")
    return f"{match.group(1).upper()}-{match.group(2)}{match.group(3)}"


def normalized_system_version(system_cnf: str) -> str:
    match = re.search(r"^\s*VER\s*=\s*([0-9]+[.][0-9]+)\s*$", system_cnf,
                      flags=re.IGNORECASE | re.MULTILINE)
    if not match:
        raise InstallError("SYSTEM.CNF does not contain a recognized VER entry")
    return match.group(1)


def find_casefold(root: Path, filename: str) -> list[Path]:
    target = filename.casefold()
    return sorted(
        (path for path in root.rglob("*") if path.is_file() and path.name.casefold() == target),
        key=lambda path: (len(path.parts), str(path).casefold()),
    )


def locate_ps2_ark(role: str, root: Path, source: Path, source_hash: str,
                   extracted_from_image: bool,
                   require_arks: bool = True) -> ArkSource:
    system_files = find_casefold(root, "SYSTEM.CNF")
    if not system_files:
        raise InstallError(f"{role}: SYSTEM.CNF not found below {root}")
    system_text = system_files[0].read_text(encoding="ascii", errors="replace")
    disc_id = normalized_disc_id(system_text)
    system_version = normalized_system_version(system_text)
    profile = PS2_DISC_PROFILES[role]
    if disc_id not in profile["disc_ids"]:
        raise InstallError(
            f"{role}: expected disc ID {sorted(profile['disc_ids'])}, found {disc_id}"
        )
    if system_version not in profile["system_versions"]:
        raise InstallError(
            f"{role}: unsupported SYSTEM.CNF revision {system_version}; expected "
            f"{sorted(profile['system_versions'])}"
        )
    headers = find_casefold(root, "MAIN.HDR")
    if not headers:
        raise InstallError(f"{role}: MAIN.HDR not found below {root}")
    hdr = headers[0]
    arks = sorted(
        (
            path
            for path in hdr.parent.iterdir()
            if path.is_file()
            and re.fullmatch(r"main_\d+[.]ark", path.name, re.IGNORECASE)
        ),
        key=lambda path: int(re.search(r"(\d+)", path.stem).group(1)),
    )
    if require_arks and not arks:
        raise InstallError(f"{role}: no MAIN_*.ARK parts beside {hdr}")
    intro_video: Path | None = None
    if role == "gh2":
        intro_files = find_casefold(root, "INTRO.PSS")
        if intro_files:
            intro_video = intro_files[0].resolve()
        elif require_arks:
            raise InstallError(
                f"{role}: retail boot video INTRO.PSS not found below {root}"
            )
    return ArkSource(
        role=role,
        source=source.resolve(),
        disc_id=disc_id,
        system_version=system_version,
        hdr=hdr.resolve(),
        arks=tuple(path.resolve() for path in arks),
        source_sha256=source_hash,
        extracted_from_image=extracted_from_image,
        intro_video=intro_video,
    )


def prepare_ps2_source(role: str, source: Path, work_root: Path, seven_zip: Path,
                       journal: list[dict[str, Any]],
                       require_arks: bool = True,
                       cache_events: list[dict[str, Any]] | None = None) -> ArkSource:
    source = source.resolve()
    if not source.exists():
        raise InstallError(f"{role}: source does not exist: {source}")
    if source.is_dir():
        critical = find_casefold(source, "SYSTEM.CNF") + find_casefold(source, "MAIN.HDR")
        headers = find_casefold(source, "MAIN.HDR")
        if headers:
            critical.extend(
                path
                for path in headers[0].parent.iterdir()
                if path.is_file() and re.fullmatch(r"main_\d+[.]ark", path.name, re.IGNORECASE)
            )
        if not critical:
            raise InstallError(f"{role}: no PS2 disc/ARK files found below {source}")
        return locate_ps2_ark(
            role, source, source, tree_fingerprint(critical), False,
            require_arks
        )

    source_hash = sha256_file(source)
    extraction_root = work_root / "media" / f"{role}-{source_hash[:16]}"
    marker = extraction_root / "source.json"
    marker_expected = {"source": str(source), "sha256": source_hash}
    marker_value = read_cache_marker(marker, marker_expected)
    if marker_value is None:
        if extraction_root.exists():
            remove_tree(extraction_root)
        extraction_root.mkdir(parents=True)
        selected = ["SYSTEM.CNF", "MAIN.HDR"]
        if require_arks:
            selected.append("MAIN_*.ARK")
            if role == "gh2":
                selected.append("INTRO.PSS")
        run(
            [str(seven_zip), "x", "-y", "-r", f"-o{extraction_root}", str(source), *selected],
            journal,
        )
        marker_value = {
            **marker_expected,
            "metadata_complete": True,
            "arks_complete": require_arks,
            "boot_video_complete": require_arks and role == "gh2",
        }
        write_json(marker, marker_value)
        record_cache_event(
            cache_events, extraction_root, "created", source_hash
        )
    else:
        record_cache_event(
            cache_events, extraction_root, "reused", source_hash
        )
    if not find_casefold(extraction_root, "MAIN.HDR"):
        run(
            [str(seven_zip), "x", "-y", "-r", f"-o{extraction_root}", str(source), "MAIN.HDR"],
            journal,
        )
    # A previous --plan cache deliberately has no large ARK part. Promote it
    # in place only when a real install is requested.
    if require_arks and not bool(marker_value.get("arks_complete")):
        run(
            [str(seven_zip), "x", "-y", "-r", f"-o{extraction_root}", str(source), "MAIN_*.ARK"],
            journal,
        )
        marker_value["arks_complete"] = True
        write_json(marker, marker_value)
        record_cache_event(
            cache_events, extraction_root, "promoted_to_full_ark", source_hash
        )
    # Older/full caches predate the loose retail boot-video requirement. Promote
    # those caches in place without re-extracting the multi-gigabyte ARK parts.
    if (
        role == "gh2"
        and require_arks
        and (
            not bool(marker_value.get("boot_video_complete"))
            or not find_casefold(extraction_root, "INTRO.PSS")
        )
    ):
        run(
            [str(seven_zip), "x", "-y", "-r", f"-o{extraction_root}", str(source), "INTRO.PSS"],
            journal,
        )
        marker_value["boot_video_complete"] = True
        write_json(marker, marker_value)
        record_cache_event(
            cache_events, extraction_root, "promoted_with_boot_video", source_hash
        )
    return locate_ps2_ark(
        role, extraction_root, source, source_hash, True, require_arks
    )


def indexed_content(content_root: Path) -> tuple[list[str], list[dict[str, Any]]]:
    paths: list[str] = []
    rows: list[dict[str, Any]] = []
    for path in sorted((p for p in content_root.rglob("*") if p.is_file())):
        relative = path.relative_to(content_root).as_posix()
        paths.append(relative)
        rows.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return paths, rows


def write_package_index(package_root: Path) -> list[str]:
    files, rows = indexed_content(package_root / "content")
    if not files:
        raise InstallError(f"package has no content: {package_root}")
    write_json(
        package_root / "content-index.json",
        {"schema_version": 1, "files": rows},
    )
    return files


def normalized_package_path(value: str) -> str:
    path = value.replace("\\", "/")
    parts = path.split("/")
    if (
        not path or path.startswith("/") or re.match(r"^[A-Za-z]:", path)
        or any(part in {"", ".", ".."} for part in parts)
    ):
        raise InstallError(f"invalid package content path: {value!r}")
    return "/".join(parts)


def resolve_below(root: Path, value: str, label: str) -> Path:
    root = root.resolve()
    candidate = (root / value).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise InstallError(f"{label} escapes its release root: {value}") from error
    return candidate


def validate_package(package: Path, require_directory_id: bool = True) -> dict[str, Any]:
    """Verify one package without trusting its manifest or content index."""
    package = package.resolve()
    manifest_path = package / "manifest.json"
    if manifest_path.is_symlink():
        raise InstallError(f"package manifest must not be a symbolic link: {package}")
    manifest = load_manifest(manifest_path)
    package_id = manifest.get("id")
    if (
        not isinstance(package_id, str) or not package_id
        or (require_directory_id and package.name != package_id)
    ):
        raise InstallError(f"package directory/id mismatch: {package}")
    if manifest.get("content_root", "content") != "content":
        raise InstallError(f"{package_id}: unsupported content_root")
    values = manifest.get("files")
    if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
        raise InstallError(f"{package_id}: manifest files must be a string array")
    normalized = [normalized_package_path(value) for value in values]
    folded = [value.casefold() for value in normalized]
    if len(folded) != len(set(folded)):
        raise InstallError(f"{package_id}: manifest contains duplicate content paths")
    if normalized != sorted(normalized):
        raise InstallError(f"{package_id}: manifest files are not deterministically sorted")

    index_path = package / str(manifest.get("content_index", "content-index.json"))
    if index_path.is_symlink():
        raise InstallError(f"{package_id}: content index must not be a symbolic link")
    index = load_manifest(index_path)
    rows = index.get("files")
    if index.get("schema_version") != 1 or not isinstance(rows, list):
        raise InstallError(f"{package_id}: invalid content index")
    indexed: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("path"), str):
            raise InstallError(f"{package_id}: invalid content-index row")
        path = normalized_package_path(row["path"])
        key = path.casefold()
        if key in indexed:
            raise InstallError(f"{package_id}: duplicate content-index path: {path}")
        if not isinstance(row.get("size"), int) or row["size"] < 0:
            raise InstallError(f"{package_id}: invalid indexed size: {path}")
        if not isinstance(row.get("sha256"), str) or not re.fullmatch(
            r"[0-9a-fA-F]{64}", row["sha256"]
        ):
            raise InstallError(f"{package_id}: invalid indexed SHA-256: {path}")
        indexed[key] = row
    if set(folded) != set(indexed):
        raise InstallError(f"{package_id}: manifest/content-index path sets differ")

    content_root = package / "content"
    actual_paths = {}
    if content_root.is_dir():
        for path in content_root.rglob("*"):
            if path.is_symlink():
                raise InstallError(f"{package_id}: symbolic links are not package content")
            if path.is_file():
                actual_paths[path.relative_to(content_root).as_posix().casefold()] = path
    if set(actual_paths) != set(indexed):
        raise InstallError(f"{package_id}: indexed and on-disk content path sets differ")
    total_size = 0
    for key, row in indexed.items():
        path = actual_paths[key]
        size = path.stat().st_size
        digest = sha256_file(path)
        if size != row["size"] or digest.casefold() != row["sha256"].casefold():
            raise InstallError(f"{package_id}: content hash mismatch: {row['path']}")
        total_size += size
    return {
        "id": package_id,
        "files": len(indexed),
        "bytes": total_size,
        "sha256": package_fingerprint(package),
    }


def song_catalog_assets(
    catalog_path: Path,
    dtb_tool: Path,
    journal: list[dict[str, Any]],
) -> list[tuple[str, str, str]]:
    output = run([str(dtb_tool), "song-assets", str(catalog_path)], journal)
    lines = [line for line in output.splitlines() if line.strip()]
    if not lines or lines[0] != "song_id\tmidi_file\taudio_file":
        raise InstallError("song asset inventory has an invalid header")
    song_ids: set[str] = set()
    assets: list[tuple[str, str, str]] = []
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 3 or not fields[0]:
            raise InstallError(f"invalid song asset inventory row: {line!r}")
        song_id, midi_value, audio_value = fields
        if song_id.casefold() in song_ids:
            raise InstallError(f"duplicate song asset inventory ID: {song_id}")
        song_ids.add(song_id.casefold())
        midi_path = normalized_package_path(midi_value)
        audio_path = normalized_package_path(audio_value)
        assets.append((song_id, midi_path, audio_path))
    if not song_ids:
        raise InstallError("song catalog contains no asset records")
    return assets


def validate_song_catalog_assets(
    assets: list[tuple[str, str, str]], package_files: list[str]
) -> int:
    available = {path.casefold() for path in package_files}
    for song_id, midi_path, audio_path in assets:
        for kind, path in (("MIDI", midi_path), ("audio", audio_path)):
            if path.casefold() not in available:
                raise InstallError(
                    f"song {song_id} references missing {kind} asset: {path}"
                )
    return len(assets)


def build_song_package(source: ArkSource, base: ArkSource, stage_root: Path,
                       ark_tool: Path, dtb_tool: Path,
                       journal: list[dict[str, Any]],
                       base_paths: set[str]) -> Path:
    package_id, package_name = PACKAGE_NAMES[source.role]
    package_root = stage_root / package_id
    content = package_root / "content"
    catalog_path = f"config/dlc/{source.role}/songs.dtb"
    catalog_output = content / Path(catalog_path)
    ark_arguments = [str(source.hdr), *(str(path) for path in source.arks)]
    base_ark_arguments = [str(base.hdr), *(str(path) for path in base.arks)]
    source_catalog = package_root / "source-songs.dtb"
    base_catalog = package_root / "base-songs.dtb"
    base_path_inventory = package_root / "base-paths.txt"
    run(
        [
            str(ark_tool), "extract", *ark_arguments,
            "--path", "config/gen/songs.dtb", "--out", str(source_catalog),
        ],
        journal,
    )
    run(
        [
            str(ark_tool), "extract", *base_ark_arguments,
            "--path", "config/gen/songs.dtb", "--out", str(base_catalog),
        ],
        journal,
    )
    catalog_output.parent.mkdir(parents=True, exist_ok=True)
    base_path_inventory.write_text(
        "\n".join(sorted(base_paths)) + "\n", encoding="utf-8"
    )
    run(
        [
            str(dtb_tool), "filter-song-catalog", str(source_catalog),
            str(base_catalog), str(catalog_output), "--base-paths",
            str(base_path_inventory),
        ],
        journal,
    )
    source_catalog_sha256 = sha256_file(source_catalog)
    source_catalog.unlink()
    base_catalog.unlink()
    base_path_inventory.unlink()
    catalog_assets = song_catalog_assets(catalog_output, dtb_tool, journal)
    asset_prefixes = sorted(
        {
            Path(path).parent.as_posix().rstrip("/") + "/"
            for _, midi_path, audio_path in catalog_assets
            for path in (midi_path, audio_path)
        }
    )
    for prefix in asset_prefixes:
        run(
            [
                str(ark_tool), "extract-prefix", *ark_arguments,
                "--prefix", prefix, "--out", str(content),
            ],
            journal,
        )
    files = write_package_index(package_root)
    song_files = [path for path in files if path.startswith("songs/")]
    if not any(path.lower().endswith(".mid") for path in song_files):
        raise InstallError(f"{source.role}: extracted package has no MIDI files")
    if not any(path.lower().endswith(".vgs") for path in song_files):
        raise InstallError(f"{source.role}: extracted package has no VGS audio")
    catalog_song_count = validate_song_catalog_assets(catalog_assets, files)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "id": package_id,
        "name": package_name,
        "version": "disc-import-1",
        "source_game": source.role,
        "content_root": "content",
        "files": files,
        "song_catalogs": [catalog_path],
        "setlists": [
            {
                "id": f"{source.role}_disc_songs",
                "label": package_name,
                "songs": [song_id for song_id, _, _ in catalog_assets],
                "include_in_quickplay": True,
            }
        ],
        "provenance": {
            "source_role": source.role,
            "disc_id": source.disc_id,
            "source_sha256": source.source_sha256,
            "source_path_recorded_for_local_audit": str(source.source),
            "policy": "user-owned-source-no-redistribution",
            "catalog_song_count": catalog_song_count,
            "source_catalog_sha256": source_catalog_sha256,
            "catalog_policy": (
                "source records minus GH2-owned duplicate IDs and "
                "validate_ignore development records"
            ),
        },
        "content_index": "content-index.json",
    }
    write_json(package_root / "manifest.json", manifest)
    return package_root


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise InstallError(f"cannot read manifest {path}: {error}") from error
    if not isinstance(value, dict):
        raise InstallError(f"manifest is not an object: {path}")
    return value


def manifest_files(manifest_path: Path) -> set[str]:
    manifest = load_manifest(manifest_path)
    values = manifest.get("files", [])
    if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
        return set()
    return {value.replace("\\", "/").casefold() for value in values}


def manifest_replacements(manifest_path: Path) -> set[str]:
    manifest = load_manifest(manifest_path)
    values = manifest.get("replaces", [])
    if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
        raise InstallError(f"manifest replaces must be a string array: {manifest_path}")
    return {normalized_package_path(value).casefold() for value in values}


def base_ark_paths(
    source: ArkSource, ark_tool: Path, journal: list[dict[str, Any]]
) -> tuple[set[str], list[str]]:
    output = run([str(ark_tool), "paths", str(source.hdr)], journal)
    paths: set[str] = set()
    system_runtime_paths: list[str] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        archive_path = line.strip().replace("\\", "/")
        # Retail GH2 deliberately stores Harmonix engine support files under
        # ../../system/run/.  They resolve outside the game's loose-content
        # root and therefore cannot be shadowed by an installable DLC path.
        # Preserve them in the audit, but exclude them from the game-root
        # collision set.  No other traversal form is accepted.
        if archive_path.casefold().startswith("../../system/run/"):
            system_runtime_paths.append(archive_path)
            continue
        path = normalized_package_path(archive_path).casefold()
        if path in paths:
            raise InstallError(f"GH2 base contains duplicate path: {path}")
        paths.add(path)
    if not paths:
        raise InstallError("GH2 base path inventory is empty")
    return paths, system_runtime_paths


def check_package_conflicts(
    dlc_root: Path, packages: list[Path], base_paths: set[str] | None = None
) -> None:
    base_paths = base_paths or set()
    owners: dict[str, str] = {}
    if dlc_root.is_dir():
        for manifest_path in sorted(dlc_root.glob("*/manifest.json")):
            manifest = load_manifest(manifest_path)
            package_id = str(manifest.get("id", manifest_path.parent.name))
            if any(path.name == package_id for path in packages):
                continue
            replacements = manifest_replacements(manifest_path)
            for content_path in manifest_files(manifest_path):
                if content_path in base_paths and content_path not in replacements:
                    raise InstallError(
                        f"content collision: {content_path} belongs to GH2 base "
                        f"and installed package {package_id} does not declare replaces"
                    )
                owners[content_path] = package_id
    for package in packages:
        manifest_path = package / "manifest.json"
        package_id = str(load_manifest(manifest_path).get("id", package.name))
        replacements = manifest_replacements(manifest_path)
        for content_path in manifest_files(manifest_path):
            if content_path in base_paths and content_path not in replacements:
                raise InstallError(
                    f"content collision: {content_path} belongs to GH2 base and "
                    f"{package_id} does not declare replaces"
                )
            previous = owners.get(content_path)
            if previous:
                raise InstallError(
                    f"content collision: {content_path} belongs to both "
                    f"{previous} and {package_id}"
                )
            owners[content_path] = package_id


def package_fingerprint(package: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(path for path in package.rglob("*") if path.is_file()):
        digest.update(path.relative_to(package).as_posix().casefold().encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def install_package(package: Path, dlc_root: Path, replace_existing: bool) -> dict[str, Any]:
    verification = validate_package(package)
    package_id = verification["id"]
    destination = dlc_root / package_id
    staging_root = dlc_root / ".s"
    staged = staging_root / uuid.uuid4().hex
    staged.parent.mkdir(parents=True, exist_ok=True)
    source_fingerprint = package_fingerprint(package)
    if destination.is_dir():
        if package_fingerprint(destination) == source_fingerprint:
            return {"id": package_id, "status": "unchanged", "sha256": source_fingerprint}
        if not replace_existing:
            raise InstallError(
                f"package already exists with different content: {destination}; "
                "rerun with --replace-existing"
            )
    copy_tree(package, staged)
    staged_verification = validate_package(staged, require_directory_id=False)
    if staged_verification["sha256"] != source_fingerprint:
        remove_tree(staged, ignore_errors=True)
        raise InstallError(f"staged package verification failed: {package_id}")
    backup: Path | None = None
    try:
        if destination.exists():
            backup = staging_root / uuid.uuid4().hex
            os.replace(destination, backup)
        os.replace(staged, destination)
        if backup:
            remove_tree(backup)
    except BaseException:
        if destination.exists() and backup:
            remove_tree(destination, ignore_errors=True)
        if backup and backup.exists():
            os.replace(backup, destination)
        remove_tree(staged, ignore_errors=True)
        raise
    return {"id": package_id, "status": "installed", "sha256": source_fingerprint}


def install_gh2_base(source: ArkSource, base_gen: Path) -> dict[str, Any]:
    source_files = [source.hdr, *source.arks]
    source_rows = [
        {"name": path.name.upper(), "size": path.stat().st_size, "sha256": sha256_file(path)}
        for path in source_files
    ]
    existing_hdrs = find_casefold(base_gen, "MAIN.HDR") if base_gen.is_dir() else []
    if existing_hdrs:
        existing_hdr = existing_hdrs[0]
        existing_arks = sorted(
            (
                path
                for path in existing_hdr.parent.iterdir()
                if path.is_file() and re.fullmatch(r"main_\d+[.]ark", path.name, re.IGNORECASE)
            ),
            key=lambda path: int(re.search(r"(\d+)", path.stem).group(1)),
        )
        existing_files = [existing_hdr, *existing_arks]
        existing_rows = [
            {"name": path.name.upper(), "size": path.stat().st_size, "sha256": sha256_file(path)}
            for path in existing_files
        ]
        if existing_rows != source_rows:
            raise InstallError(
                f"existing base at {base_gen} is not byte-identical to the verified GH2 disc"
            )
        base_status = "unchanged"
    else:
        if base_gen.exists() and any(base_gen.iterdir()):
            raise InstallError(f"base destination exists but is not a GH2 GEN directory: {base_gen}")
        base_gen.parent.mkdir(parents=True, exist_ok=True)
        staged = base_gen.parent / f".{base_gen.name}-staging-{uuid.uuid4().hex}"
        staged.mkdir()
        try:
            for index, path in enumerate(source_files):
                name = "MAIN.HDR" if index == 0 else f"MAIN_{index - 1}.ARK"
                shutil.copyfile(path, staged / name)
            staged_rows = [
                {"name": path.name.upper(), "size": path.stat().st_size, "sha256": sha256_file(path)}
                for path in [
                    staged / "MAIN.HDR",
                    *sorted(
                        staged.glob("MAIN_*.ARK"),
                        key=lambda path: int(re.search(r"(\d+)", path.stem).group(1)),
                    ),
                ]
            ]
            if staged_rows != source_rows:
                raise InstallError("staged GH2 base hash verification failed")
            if base_gen.exists():
                base_gen.rmdir()
            os.replace(staged, base_gen)
        except BaseException:
            remove_tree(staged, ignore_errors=True)
            raise
        base_status = "installed"

    if source.intro_video is None or not source.intro_video.is_file():
        raise InstallError("verified GH2 source has no retail INTRO.PSS boot video")
    video_destination = base_gen.parent / "videos" / "intro.pss"
    source_video_row = {
        "name": "videos/intro.pss",
        "size": source.intro_video.stat().st_size,
        "sha256": sha256_file(source.intro_video),
    }
    video_status = "unchanged"
    if not video_destination.is_file() or sha256_file(video_destination) != source_video_row["sha256"]:
        video_destination.parent.mkdir(parents=True, exist_ok=True)
        staged_video = video_destination.with_name(
            video_destination.name + f".{uuid.uuid4().hex}.tmp"
        )
        try:
            shutil.copyfile(source.intro_video, staged_video)
            if sha256_file(staged_video) != source_video_row["sha256"]:
                raise InstallError("staged GH2 boot-video hash verification failed")
            os.replace(staged_video, video_destination)
        finally:
            staged_video.unlink(missing_ok=True)
        video_status = "installed"
    return {
        "status": base_status,
        "path": str(base_gen),
        "files": source_rows,
        "boot_video": {**source_video_row, "status": video_status},
    }


def validate_release_ready_package(
    release_manifest: Path, row: dict[str, Any]
) -> dict[str, Any]:
    package_id = row["id"]
    relative = row.get("path")
    if not isinstance(relative, str):
        raise InstallError(f"release package {package_id} has no path")
    qualification_relative = row.get("qualification")
    if not isinstance(qualification_relative, str):
        raise InstallError(f"release package {package_id} has no qualification record")
    qualification_path = resolve_below(
        release_manifest.parent, qualification_relative,
        f"release package {package_id} qualification",
    )
    if qualification_path.is_symlink():
        raise InstallError(f"release package {package_id} qualification is a symbolic link")
    qualification = load_manifest(qualification_path)
    if qualification.get("schema_version") != 1:
        raise InstallError(f"release package {package_id} qualification schema is invalid")
    if qualification.get("package_id") != package_id:
        raise InstallError(f"release package {package_id} qualification ID mismatch")
    redistribution = qualification.get("redistribution_basis")
    if redistribution not in {
        "project-owned-converted-no-original-assets",
        "source-disc-derived-patch",
        "project-authorized-preconverted-release",
    }:
        raise InstallError(
            f"release package {package_id} has no accepted redistribution basis"
        )
    parity = qualification.get("parity_gate")
    if not isinstance(parity, dict) or parity.get("status") != "passed":
        raise InstallError(f"release package {package_id} parity gate has not passed")
    proofs = qualification.get("proofs")
    if not isinstance(proofs, list) or not proofs:
        raise InstallError(f"release package {package_id} has no qualification proofs")
    for proof in proofs:
        if (
            not isinstance(proof, dict)
            or not isinstance(proof.get("path"), str)
            or not isinstance(proof.get("sha256"), str)
            or not re.fullmatch(r"[0-9a-fA-F]{64}", proof["sha256"])
        ):
            raise InstallError(f"release package {package_id} has an invalid proof record")
        proof_path = resolve_below(
            release_manifest.parent, proof["path"],
            f"release package {package_id} proof",
        )
        if (
            proof_path.is_symlink()
            or not proof_path.is_file()
            or sha256_file(proof_path).casefold() != proof["sha256"].casefold()
        ):
            raise InstallError(
                f"release package {package_id} qualification proof is missing or changed: "
                f"{proof['path']}"
            )
    source = resolve_below(
        release_manifest.parent, relative, f"release package {package_id} path"
    )
    if not (source / "manifest.json").is_file():
        raise InstallError(f"release package is missing: {source}")
    verification = validate_package(source, require_directory_id=False)
    if verification["id"] != package_id:
        raise InstallError(f"release package ID mismatch: {package_id}")
    return {
        "id": package_id,
        "source": source,
        "redistribution_basis": redistribution,
        "qualification_path": qualification_path,
        "qualification_sha256": sha256_file(qualification_path),
        "package_sha256": verification["sha256"],
    }


def copy_release_ready_packages(release_manifest: Path | None, stage_root: Path,
                                audit: dict[str, Any]) -> list[Path]:
    if release_manifest is None:
        audit["release_content"] = {"status": "not_requested", "packages": []}
        return []
    root = load_manifest(release_manifest)
    packages = root.get("packages", [])
    if not isinstance(packages, list):
        raise InstallError("release manifest packages must be an array")
    copied: list[Path] = []
    report: list[dict[str, Any]] = []
    for row in packages:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            raise InstallError("invalid release package row")
        package_id = row["id"]
        ready = row.get("release_ready") is True
        if not ready:
            report.append(
                {"id": package_id, "status": "excluded", "reason": row.get("reason", "not release-ready")}
            )
            continue
        validated = validate_release_ready_package(release_manifest, row)
        source = validated["source"]
        destination = stage_root / package_id
        copy_tree(source, destination)
        staged = validate_package(destination)
        if staged["sha256"] != validated["package_sha256"]:
            raise InstallError(f"release package changed while staging: {package_id}")
        copied.append(destination)
        report.append(
            {
                "id": package_id,
                "status": "included",
                "redistribution_basis": validated["redistribution_basis"],
                "qualification_sha256": validated["qualification_sha256"],
                "package_sha256": validated["package_sha256"],
            }
        )
    audit["release_content"] = {"status": "evaluated", "packages": report}
    return copied


def validate_release_content_plan(release_manifest: Path | None) -> dict[str, Any]:
    if release_manifest is None:
        return {"status": "not_requested", "packages": []}
    root = load_manifest(release_manifest)
    packages = root.get("packages", [])
    if not isinstance(packages, list):
        raise InstallError("release manifest packages must be an array")
    report: list[dict[str, Any]] = []
    for row in packages:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            raise InstallError("invalid release package row")
        if row.get("release_ready") is not True:
            report.append({
                "id": row["id"], "status": "excluded",
                "reason": row.get("reason", "not release-ready"),
            })
            continue
        validated = validate_release_ready_package(release_manifest, row)
        report.append({
            "id": row["id"], "status": "validated_for_copy",
            "redistribution_basis": validated["redistribution_basis"],
            "qualification_sha256": validated["qualification_sha256"],
            "package_sha256": validated["package_sha256"],
        })
    return {"status": "validated", "packages": report}


def validate_release_ui_assets(release_manifest: Path | None) -> dict[str, Any]:
    if release_manifest is None:
        return {"status": "not_requested", "assets": []}
    root = load_manifest(release_manifest)
    assets = root.get("ui_assets", [])
    if not isinstance(assets, list):
        raise InstallError("release manifest ui_assets must be an array")
    report: list[dict[str, Any]] = []
    for row in assets:
        if not isinstance(row, dict):
            raise InstallError("invalid release UI asset row")
        asset_id = row.get("id")
        relative = row.get("path")
        destination = row.get("destination")
        files = row.get("files")
        if (
            not isinstance(asset_id, str) or not asset_id
            or not isinstance(relative, str) or not relative
            or not isinstance(destination, str) or not destination
            or not isinstance(files, list) or not files
        ):
            raise InstallError("incomplete release UI asset row")
        normalized_package_path(destination)
        source = resolve_below(
            release_manifest.parent, relative,
            f"release UI asset {asset_id} path",
        )
        expected: dict[str, str] = {}
        for item in files:
            if (
                not isinstance(item, dict)
                or not isinstance(item.get("path"), str)
                or not isinstance(item.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-fA-F]{64}", item["sha256"])
            ):
                raise InstallError(f"{asset_id}: invalid UI asset file row")
            file_path = normalized_package_path(item["path"])
            key = file_path.casefold()
            if key in expected:
                raise InstallError(f"{asset_id}: duplicate UI asset: {file_path}")
            expected[key] = item["sha256"].casefold()
        actual = {
            path.relative_to(source).as_posix().casefold(): path
            for path in source.rglob("*") if path.is_file()
        } if source.is_dir() else {}
        if set(actual) != set(expected):
            raise InstallError(f"{asset_id}: release UI asset file set differs")
        total = 0
        for key, digest in expected.items():
            path = actual[key]
            if sha256_file(path).casefold() != digest:
                raise InstallError(f"{asset_id}: UI asset hash mismatch: {key}")
            total += path.stat().st_size
        report.append({
            "id": asset_id, "status": "validated_for_copy",
            "source": str(source), "destination": destination,
            "files": len(actual), "bytes": total,
        })
    return {"status": "validated", "assets": report}


def install_release_ui_assets(release_manifest: Path | None, base_gen: Path) -> dict[str, Any]:
    validation = validate_release_ui_assets(release_manifest)
    if release_manifest is None:
        return validation
    root = load_manifest(release_manifest)
    for row, validated in zip(root.get("ui_assets", []), validation["assets"]):
        source = resolve_below(
            release_manifest.parent, row["path"],
            f"release UI asset {row['id']} path",
        )
        destination = resolve_below(
            base_gen, row["destination"],
            f"release UI asset {row['id']} destination",
        )
        staged = destination.parent / f".{destination.name}-staging-{uuid.uuid4().hex}"
        copy_tree(source, staged)
        if destination.exists():
            remove_tree(destination)
        os.replace(staged, destination)
        validated["status"] = "installed"
        validated["destination"] = str(destination)
    validation["status"] = "installed"
    return validation


def dolphin_image_identity(
    source: Path,
    dolphin_tool: Path,
    journal: list[dict[str, Any]],
) -> tuple[str, int]:
    output = run(
        [str(dolphin_tool), "header", "-i", str(source), "--json"],
        journal,
    )
    try:
        header = json.loads(output)
        game_id = str(header["game_id"])
        revision = int(header["revision"])
    except (ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        raise InstallError(
            f"rb2_wii: DolphinTool returned an invalid image header: {error}"
        ) from error
    return game_id, revision


def prepare_rb2_source(
    source: Path,
    work_root: Path,
    dolphin_tool: Path | None,
    ark_helper: Path,
    journal: list[dict[str, Any]],
    cache_events: list[dict[str, Any]] | None = None,
    dtab_tool: Path | None = None,
) -> tuple[Path, str, str, int | None]:
    source = source.resolve()
    if not source.exists():
        raise InstallError(f"rb2_wii: source does not exist: {source}")
    if source.is_file():
        source_hash = sha256_file(source)
        disc_id = "unavailable"
        disc_revision: int | None = None
        if source.suffix.casefold() == ".iso":
            with source.open("rb") as stream:
                header = stream.read(8)
            header_id = header[:6].decode("ascii", errors="replace")
            if header_id != RB2_WII_DISC_ID:
                raise InstallError(
                    f"rb2_wii: expected Wii disc ID {RB2_WII_DISC_ID}, found {header_id!r}"
                )
            disc_id = header_id
            disc_revision = header[7]
        if not dolphin_tool or not dolphin_tool.is_file():
            raise InstallError(
                "rb2_wii: DolphinTool is required for Wii disc images; "
                "pass --dolphin-tool or supply an already-extracted disc root"
            )
        tool_disc_id, tool_disc_revision = dolphin_image_identity(
            source, dolphin_tool, journal
        )
        if tool_disc_id != RB2_WII_DISC_ID:
            raise InstallError(
                f"rb2_wii: expected Wii disc ID {RB2_WII_DISC_ID}, found "
                f"{tool_disc_id!r}"
            )
        if tool_disc_revision not in RB2_WII_DISC_REVISIONS:
            raise InstallError(
                f"rb2_wii: unsupported disc revision {tool_disc_revision}; "
                f"expected {sorted(RB2_WII_DISC_REVISIONS)}"
            )
        if disc_id not in {"unavailable", tool_disc_id}:
            raise InstallError("rb2_wii: image and DolphinTool disc IDs disagree")
        disc_id = tool_disc_id
        disc_revision = tool_disc_revision
        media_root = work_root / "media" / f"rb2_wii-{source_hash[:16]}"
        disc_root = media_root / "disc"
        marker = media_root / "source.json"
        marker_expected = {"source": str(source), "sha256": source_hash}
        marker_value = read_cache_marker(marker, marker_expected)
        rebuilt_disc = marker_value is None or not disc_root.is_dir()
        if rebuilt_disc:
            if media_root.exists():
                remove_tree(media_root)
            disc_root.mkdir(parents=True)
            run(
                [str(dolphin_tool), "extract", "-i", str(source), "-o", str(disc_root), "-g"],
                journal,
            )
        boot_files = find_casefold(disc_root, "boot.bin")
        if boot_files:
            with boot_files[0].open("rb") as stream:
                header = stream.read(8)
            extracted_id = header[:6].decode("ascii", errors="replace")
            if extracted_id != RB2_WII_DISC_ID:
                raise InstallError(
                    f"rb2_wii: extracted disc ID should be {RB2_WII_DISC_ID}, "
                    f"found {extracted_id!r}"
                )
            if disc_id not in {"unavailable", extracted_id}:
                raise InstallError("rb2_wii: image and extracted disc IDs disagree")
            disc_id = extracted_id
            disc_revision = header[7]
        if rebuilt_disc:
            write_json(
                marker,
                {
                    **marker_expected,
                    "disc_extract_complete": True,
                    "disc_id": disc_id,
                    "disc_revision": disc_revision,
                },
            )
            record_cache_event(cache_events, media_root, "created", source_hash)
        else:
            record_cache_event(cache_events, media_root, "reused", source_hash)
    else:
        source_hash = tree_fingerprint(
            path for path in source.rglob("*") if path.is_file()
        )
        disc_id = "structure-validated"
        disc_revision = None
        disc_root = source
        boot_files = find_casefold(disc_root, "boot.bin")
        if boot_files:
            with boot_files[0].open("rb") as stream:
                header = stream.read(8)
            disc_id = header[:6].decode("ascii", errors="replace")
            disc_revision = header[7]

    if disc_id not in {RB2_WII_DISC_ID, "structure-validated"}:
        raise InstallError(f"rb2_wii: expected disc ID {RB2_WII_DISC_ID}, found {disc_id!r}")
    if disc_revision is not None and disc_revision not in RB2_WII_DISC_REVISIONS:
        raise InstallError(
            f"rb2_wii: unsupported disc revision {disc_revision}; expected "
            f"{sorted(RB2_WII_DISC_REVISIONS)}"
        )

    # An already-extracted ArkHelper tree can be reused without duplicating it.
    candidates = [disc_root / "source_ark"]
    if (disc_root / "char/instruments.dta").is_file():
        candidates.insert(0, disc_root)
    source_ark = next((path for path in candidates if (path / "char/instruments.dta").is_file()), None)
    if source_ark is not None:
        rb2_root = source_ark.parent
    else:
        headers = sorted(
            path
            for path in disc_root.rglob("*")
            if path.is_file() and path.name.casefold() == "main_wii.hdr"
        )
        if not headers:
            raise InstallError(
                f"rb2_wii: DATA/files/gen/main_wii.hdr not found below {disc_root}"
            )
        rb2_root = work_root / "rb2" / source_hash[:16]
        source_ark = rb2_root / "source_ark"
        source_ark_marker = rb2_root / "source-ark.json"
        marker_expected = {"source_sha256": source_hash}
        required_relative = [
            "char/instruments.dta",
            "config/colorindex.dta",
            "char/gen/colorpalettes.milo_wii",
            "ui/eng/locale_og.dta",
        ]
        marker_value = read_cache_marker(source_ark_marker, marker_expected)
        source_ark_ready = marker_value is not None and all(
            (source_ark / relative).is_file() for relative in required_relative
        )
        if not source_ark_ready:
            if dtab_tool is None or not dtab_tool.is_file():
                raise InstallError(
                    "rb2_wii: dtab.exe is required to decode the retail RB2 "
                    "instrument and locale catalogs"
                )
            if source_ark.exists():
                remove_tree(source_ark)
            source_ark.mkdir(parents=True)
            run(
                [
                    str(ark_helper), "ark2dir", str(headers[0]), str(source_ark),
                    "--extractAll", "--convertScripts", "--logLevel", "info",
                ],
                journal,
                path_prepend=dtab_tool.parent,
            )
            missing_after_extract = [
                str(source_ark / relative)
                for relative in required_relative
                if not (source_ark / relative).is_file()
            ]
            if missing_after_extract:
                raise InstallError(
                    "rb2_wii: ArkHelper extraction completed without required "
                    "catalog files: " + ", ".join(missing_after_extract)
                )
            write_json(
                source_ark_marker,
                {**marker_expected, "ark_extract_complete": True},
            )
            record_cache_event(
                cache_events, rb2_root, "created", source_hash
            )
        else:
            record_cache_event(
                cache_events, rb2_root, "reused", source_hash
            )
    required = [
        source_ark / "char/instruments.dta",
        source_ark / "config/colorindex.dta",
        source_ark / "char/gen/colorpalettes.milo_wii",
        source_ark / "ui/eng/locale_og.dta",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise InstallError(
            "rb2_wii: source structure is not the expected retail RB2 catalog: "
            + ", ".join(missing)
        )
    return source_ark.resolve(), source_hash, disc_id, disc_revision


def build_gh80_character_package(
    gh2: ArkSource,
    gh80: ArkSource,
    stage_root: Path,
    ghogx: Path,
    ark_tool: Path,
    journal: list[dict[str, Any]],
) -> Path:
    output = stage_root / "disc.gh80s.characters"
    script = installer_helper("build_gh80_character_package.py")
    run(
        python_tool_command(
            script, "--ghogx", str(ghogx),
            "--ark-tool", str(ark_tool), "--gh2-root", str(gh2.hdr.parent),
            "--gh80-root", str(gh80.hdr.parent), "--output", str(output),
        ),
        journal,
    )
    manifest_path = output / "manifest.json"
    manifest = load_manifest(manifest_path)
    manifest["provenance"].update(
        {
            "gh2_disc_id": gh2.disc_id,
            "gh2_source_sha256": gh2.source_sha256,
            "gh80s_disc_id": gh80.disc_id,
            "gh80s_source_sha256": gh80.source_sha256,
        }
    )
    write_json(manifest_path, manifest)
    return output


def build_rb2_package(
    source_ark: Path,
    source_hash: str,
    gh2: ArkSource,
    stage_root: Path,
    work_root: Path,
    args: argparse.Namespace,
    journal: list[dict[str, Any]],
) -> Path:
    repo = resource_root()
    rb2_tools = repo / "rb2_wii/tools"
    rb2_root = work_root / "rb2-build" / source_hash[:16]
    build_root = rb2_root / "dlc_build"
    catalog = rb2_root / "catalog"
    inventory = catalog / "rb2_instruments.tsv"
    finishes = catalog / "rb2_instrument_finishes.tsv"
    run(
        python_tool_command(
            rb2_tools / "build_instrument_inventory.py",
            "--rb2-root", str(rb2_root), "--source-root", str(source_ark),
            "--output", str(inventory),
        ),
        journal,
    )
    run(
        python_tool_command(
            rb2_tools / "build_instrument_finish_inventory.py",
            "--rb2-root", str(rb2_root), "--inventory", str(inventory),
            "--source-root", str(source_ark), "--output", str(finishes),
        ),
        journal,
    )
    template_milo = build_root / "template/guitar_sg.milo_ps2"
    template_dir = build_root / "template/stock_sg"
    if not template_dir.is_dir():
        run(
            [
                str(args.ark_tool), "extract", str(gh2.hdr),
                *(str(path) for path in gh2.arks), "--path",
                "char/og/guitars/gen/guitar_sg.milo_ps2", "--out",
                str(template_milo),
            ],
            journal,
        )
        run(
            [str(args.superfreq), "milo2dir", str(template_milo), str(template_dir), "--preset", "gh2"],
            journal,
        )
    conversion = build_root / "conversion"
    conversion_command = python_tool_command(
        rb2_tools / "convert_rb2_instruments.py",
        "--rb2-root", str(rb2_root), "--inventory", str(finishes),
        "--source-root", str(source_ark),
        "--output-root", str(conversion), "--milo-tool", str(args.milo_tool),
        "--tex-tool", str(args.tex_tool), "--superfreq", str(args.superfreq),
        "--template", str(template_dir),
    )
    if conversion.is_dir():
        conversion_command.append("--resume")
    run(conversion_command, journal)
    output = stage_root / "disc.rb2_wii.instruments"
    run(
        python_tool_command(
            rb2_tools / "build_rb2_dlc_package.py",
            "--inventory", str(inventory), "--records",
            str(conversion / "conversion_records.tsv"), "--overlay",
            str(conversion / "overlay/char/og/guitars/gen"), "--output",
            str(output), "--source-sha256", source_hash,
        ),
        journal,
    )
    return output


def parse_args() -> argparse.Namespace:
    repo = resource_root()
    embedded = repo / "_embedded"
    packaged_superfreq = embedded / "superfreq.exe"
    developer_superfreq = repo.parent / "_community_re/Guitar-Hero-II-Deluxe-Unified/dependencies/windows/superfreq.exe"
    parser = argparse.ArgumentParser(
        description="Install user-owned GH1/GH80s/RB2 content as loose GHOGX DLC"
    )
    parser.add_argument("--gh2", required=True, type=Path, help="GH2 PS2 disc image or extracted root")
    parser.add_argument("--gh1", type=Path, help="GH1 PS2 disc image or extracted root")
    parser.add_argument("--gh80s", type=Path, help="GH80s PS2 disc image or extracted root")
    parser.add_argument("--rb2-wii", type=Path, help="RB2 Wii image or extracted root")
    parser.add_argument("--dlc-root", required=True, type=Path)
    parser.add_argument("--base-gen", type=Path, help="stock GH2 GEN destination; defaults beside DLC")
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--ark-tool", type=Path, default=repo / "engine/out/build/win-amd64-release/_tools_ark/ark_tool.exe")
    parser.add_argument("--dtb-tool", type=Path, default=repo / "engine/out/build/win-amd64-release/_tools_dtb/dtb_tool.exe")
    bundled_seven_zip = embedded / "7z.exe"
    discovered_seven_zip = shutil.which("7z") or shutil.which("7zz")
    common_seven_zip = Path(r"C:\Program Files\7-Zip\7z.exe")
    default_seven_zip = (
        bundled_seven_zip if bundled_seven_zip.is_file()
        else Path(discovered_seven_zip) if discovered_seven_zip
        else common_seven_zip if common_seven_zip.is_file()
        else None
    )
    parser.add_argument(
        "--seven-zip", type=Path, default=default_seven_zip,
        help="7z/7zz executable; required only when a PS2 source is an image",
    )
    parser.add_argument("--release-manifest", type=Path, default=repo / "release/dlc-content.json")
    parser.add_argument("--ghogx", type=Path, default=repo / "engine/out/build/win-amd64-release/ghogx.exe")
    parser.add_argument("--milo-tool", type=Path, default=repo / "engine/out/build/win-amd64-release/_tools_milo/milo_tool.exe")
    parser.add_argument("--tex-tool", type=Path, default=repo / "engine/out/build/win-amd64-release/_tools_tex/tex_tool.exe")
    embedded_ark_helper = embedded / "arkhelper.exe"
    community_ark_helper = repo.parent / "_community_re/Guitar-Hero-II-Deluxe-Unified/dependencies/windows/arkhelper.exe"
    source_ark_helper = repo / "third_party/Mackiloha/Src/Apps/ArkHelper/bin/Release/net8.0/win-x64/native/arkhelper.exe"
    parser.add_argument(
        "--ark-helper", type=Path,
        default=(
            embedded_ark_helper if embedded_ark_helper.is_file()
            else community_ark_helper if community_ark_helper.is_file()
            else source_ark_helper
        ),
    )
    parser.add_argument(
        "--superfreq", type=Path,
        default=packaged_superfreq if packaged_superfreq.is_file() else developer_superfreq,
    )
    embedded_dolphin = embedded / "DolphinTool.exe"
    parser.add_argument(
        "--dolphin-tool", type=Path,
        default=embedded_dolphin if embedded_dolphin.is_file() else None,
    )
    embedded_dtab = embedded / "dtab.exe"
    developer_dtab = repo.parent / "_community_re/Guitar-Hero-II-Deluxe-Unified/dependencies/windows/dtab.exe"
    parser.add_argument(
        "--dtab-tool", type=Path,
        default=embedded_dtab if embedded_dtab.is_file() else developer_dtab,
    )
    parser.add_argument("--replace-existing", action="store_true")
    parser.add_argument("--keep-work", action="store_true")
    parser.add_argument("--plan", action="store_true", help="validate inputs and report actions without extracting packages")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    dlc_root = args.dlc_root.resolve()
    base_gen = (args.base_gen or (dlc_root.parent / "gen")).resolve()
    work_root = (args.work_root or (dlc_root / ".installer-work")).resolve()
    run_id = f"{dt.datetime.now().strftime('%Y%m%dT%H%M%S')}-{uuid.uuid4().hex[:8]}"
    run_root = work_root / "r" / run_id.rsplit("-", 1)[-1]
    stage_root = run_root / "p"
    stage_root.mkdir(parents=True, exist_ok=True)
    journal: list[dict[str, Any]] = []
    audit: dict[str, Any] = {
        "schema_version": 1,
        "run_id": run_id,
        "started_utc": utc_now(),
        "status": "running",
        "policy": {
            "base_game": "verified GH2 archive; never modified",
            "non_default_content": "loose removable DLC only",
            "source_media": "read-only user-owned media",
        },
        "sources": [],
        "commands": journal,
        "cache_events": [],
        "packages": [],
        "base": {"path": str(base_gen), "status": "pending"},
        "runtime": {"python": sys.version, "executable": sys.executable},
        "tools": [],
        "implementation": [],
    }
    audit_path = dlc_root / ".install-audit" / f"{run_id}.json"
    try:
        if not args.plan:
            emit_progress(15, "Preparing and validating source media…")
        if not args.ark_tool.is_file():
            raise InstallError(f"required tool not found: {args.ark_tool}")
        ps2_image_requested = any(
            path is not None and path.is_file()
            for path in (args.gh2, args.gh1, args.gh80s)
        )
        if ps2_image_requested and (
            args.seven_zip is None or not args.seven_zip.is_file()
        ):
            raise InstallError(
                "7-Zip is required for PS2 disc images; pass --seven-zip, "
                "place 7z on PATH, or supply mounted/extracted disc roots"
            )
        for name, tool in (
            ("seven_zip", args.seven_zip), ("ark_tool", args.ark_tool),
            ("dtb_tool", args.dtb_tool),
            ("ghogx", args.ghogx), ("milo_tool", args.milo_tool),
            ("tex_tool", args.tex_tool), ("ark_helper", args.ark_helper),
            ("superfreq", args.superfreq), ("dolphin_tool", args.dolphin_tool),
            ("dtab_tool", args.dtab_tool),
        ):
            if tool and tool.is_file():
                audit["tools"].append(
                    {"name": name, "path": str(tool.resolve()),
                     "size": tool.stat().st_size, "sha256": sha256_file(tool)}
                )
        repo = resource_root()
        implementation_files = [
            Path(__file__).resolve(),
            installer_helper("build_gh80_character_package.py"),
            repo / "tools/build_character_variant_overlay.py",
            repo / "rb2_wii/tools/build_instrument_inventory.py",
            repo / "rb2_wii/tools/build_instrument_finish_inventory.py",
            repo / "rb2_wii/tools/convert_rb2_instruments.py",
            repo / "rb2_wii/tools/build_rb2_dlc_package.py",
        ]
        for path in implementation_files:
            if path.is_file():
                audit["implementation"].append(
                    {
                        "path": str(path.resolve()),
                        "size": path.stat().st_size,
                        "sha256": sha256_file(path),
                    }
                )
        sources: dict[str, ArkSource] = {}
        for role, source_path in (("gh2", args.gh2), ("gh1", args.gh1), ("gh80s", args.gh80s)):
            if source_path is None:
                continue
            source = prepare_ps2_source(
                role, source_path, work_root, args.seven_zip, journal,
                require_arks=not args.plan,
                cache_events=audit["cache_events"],
            )
            sources[role] = source
            audit["sources"].append(
                {
                    "role": role,
                    "path": str(source.source),
                    "disc_id": source.disc_id,
                    "system_version": source.system_version,
                    "sha256": source.source_sha256,
                    "hdr": str(source.hdr),
                    "ark_parts": [str(path) for path in source.arks],
                    "read_only": True,
                }
            )
            run(
                [
                    str(args.ark_tool), "verify", str(source.hdr),
                    *(str(path) for path in source.arks),
                ],
                journal,
            )
        if "gh2" not in sources:
            raise InstallError("GH2 base validation is mandatory")
        if "gh80s" in sources and not args.ghogx.is_file():
            raise InstallError(f"GH80s character catalog tool not found: {args.ghogx}")
        if any(role in sources for role in ("gh1", "gh80s")) and not args.dtb_tool.is_file():
            raise InstallError(f"song catalog inventory tool not found: {args.dtb_tool}")
        rb2_prepared: tuple[Path, str, str, int | None] | None = None
        if args.rb2_wii and not args.plan:
            for tool in (
                args.ark_helper, args.superfreq, args.milo_tool,
                args.tex_tool, args.dtab_tool,
            ):
                if not tool.is_file():
                    raise InstallError(f"required RB2 conversion tool not found: {tool}")
            rb2_prepared = prepare_rb2_source(
                args.rb2_wii, work_root, args.dolphin_tool, args.ark_helper,
                journal, audit["cache_events"], dtab_tool=args.dtab_tool,
            )
            audit["sources"].append(
                {
                    "role": "rb2_wii", "path": str(args.rb2_wii.resolve()),
                    "disc_id": rb2_prepared[2], "sha256": rb2_prepared[1],
                    "disc_revision": rb2_prepared[3],
                    "read_only": True,
                }
            )
        elif args.rb2_wii:
            rb2_plan_source = args.rb2_wii.resolve()
            if not rb2_plan_source.exists():
                raise InstallError(f"rb2_wii: source does not exist: {rb2_plan_source}")
            for tool in (
                args.ark_helper, args.superfreq, args.milo_tool,
                args.tex_tool, args.dtab_tool,
            ):
                if not tool.is_file():
                    raise InstallError(f"required RB2 conversion tool not found: {tool}")
            if rb2_plan_source.is_file() and (
                not args.dolphin_tool or not args.dolphin_tool.is_file()
            ):
                raise InstallError(
                    "rb2_wii: DolphinTool is required for Wii disc images"
                )
            rb2_plan_id = "deferred-structure-validation"
            rb2_plan_revision: int | None = None
            if rb2_plan_source.is_file():
                rb2_plan_id, rb2_plan_revision = dolphin_image_identity(
                    rb2_plan_source, args.dolphin_tool, journal
                )
                if rb2_plan_id != RB2_WII_DISC_ID:
                    raise InstallError(
                        f"rb2_wii: expected Wii disc ID {RB2_WII_DISC_ID}, found {rb2_plan_id!r}"
                    )
                if rb2_plan_revision not in RB2_WII_DISC_REVISIONS:
                    raise InstallError(
                        f"rb2_wii: unsupported disc revision {rb2_plan_revision}"
                    )
            elif rb2_plan_source.is_dir():
                boot_files = find_casefold(rb2_plan_source, "boot.bin")
                if boot_files:
                    with boot_files[0].open("rb") as stream:
                        rb2_header = stream.read(8)
                    rb2_plan_id = rb2_header[:6].decode("ascii", errors="replace")
                    rb2_plan_revision = rb2_header[7]
                    if rb2_plan_id != RB2_WII_DISC_ID:
                        raise InstallError(
                            f"rb2_wii: expected extracted disc ID "
                            f"{RB2_WII_DISC_ID}, found {rb2_plan_id!r}"
                        )
                    if rb2_plan_revision not in RB2_WII_DISC_REVISIONS:
                        raise InstallError(
                            f"rb2_wii: unsupported disc revision {rb2_plan_revision}"
                        )
                source_ark_candidates = [
                    rb2_plan_source,
                    rb2_plan_source / "source_ark",
                ]
                source_ark = next(
                    (
                        path for path in source_ark_candidates
                        if (path / "char/instruments.dta").is_file()
                    ),
                    None,
                )
                if source_ark is not None:
                    required = [
                        source_ark / "char/instruments.dta",
                        source_ark / "config/colorindex.dta",
                        source_ark / "char/gen/colorpalettes.milo_wii",
                        source_ark / "ui/eng/locale_og.dta",
                    ]
                    missing = [str(path) for path in required if not path.is_file()]
                    if missing:
                        raise InstallError(
                            "rb2_wii: extracted source catalog is incomplete: "
                            + ", ".join(missing)
                        )
                    if rb2_plan_id == "deferred-structure-validation":
                        rb2_plan_id = "structure-validated"
                elif not find_casefold(rb2_plan_source, "main_wii.hdr"):
                    raise InstallError(
                        "rb2_wii: extracted root has neither a validated "
                        "source_ark catalog nor DATA/files/gen/main_wii.hdr"
                    )
            audit["sources"].append(
                {"role": "rb2_wii", "path": str(rb2_plan_source),
                 "disc_id": rb2_plan_id, "disc_revision": rb2_plan_revision,
                 "status": "deferred_by_plan",
                 "read_only": True}
            )
        if not args.plan:
            emit_progress(42, "Source media validated.")
        release_manifest = args.release_manifest.resolve() if args.release_manifest else None
        if release_manifest and not release_manifest.is_file():
            raise InstallError(f"release manifest not found: {release_manifest}")
        audit["release_content"] = validate_release_content_plan(release_manifest)
        audit["release_ui_assets"] = validate_release_ui_assets(release_manifest)
        if args.plan:
            audit["base"] = {
                "path": str(base_gen),
                "status": "would_validate_or_install_byte_identical_gh2",
            }
            audit["status"] = "planned"
            audit["completed_utc"] = utc_now()
            write_json(audit_path, audit)
            print(f"DLC_INSTALL_PLAN_OK audit={audit_path}")
            return 0

        packages: list[Path] = []
        emit_progress(48, "Indexing the byte-identical GH2 base…")
        base_paths, base_system_runtime_paths = base_ark_paths(
            sources["gh2"], args.ark_tool, journal
        )
        base_path_digest = hashlib.sha256(
            "\n".join(sorted(base_paths)).encode("utf-8")
        ).hexdigest()
        audit["base_index"] = {
            "paths": len(base_paths),
            "sha256": base_path_digest,
            "system_runtime_paths": len(base_system_runtime_paths),
            "system_runtime_sha256": hashlib.sha256(
                "\n".join(sorted(base_system_runtime_paths)).encode("utf-8")
            ).hexdigest(),
        }
        for role in ("gh1", "gh80s"):
            if role in sources:
                packages.append(
                    build_song_package(
                        sources[role], sources["gh2"], stage_root, args.ark_tool,
                        args.dtb_tool, journal, base_paths,
                    )
                )
        emit_progress(62, "Song packages built and verified.")
        if "gh80s" in sources:
            if not args.ghogx.is_file():
                raise InstallError(f"GH80s character catalog tool not found: {args.ghogx}")
            packages.append(
                build_gh80_character_package(
                    sources["gh2"], sources["gh80s"], stage_root,
                    args.ghogx, args.ark_tool, journal,
                )
            )
        emit_progress(72, "Character packages built and verified.")
        if release_manifest and release_manifest.is_file():
            packages.extend(copy_release_ready_packages(release_manifest, stage_root, audit))
        elif args.release_manifest:
            raise InstallError(f"release manifest not found: {release_manifest}")
        emit_progress(78, "Bundled converted content verified.")
        if rb2_prepared:
            emit_progress(80, "Converting Rock Band 2 instruments…")
            packages.append(
                build_rb2_package(
                    rb2_prepared[0], rb2_prepared[1], sources["gh2"],
                    stage_root, work_root, args, journal,
                )
            )
        emit_progress(91, "All selected DLC packages are ready.")
        check_package_conflicts(dlc_root, packages, base_paths)
        emit_progress(94, "Installing and verifying the base and DLC…")
        audit["base"] = install_gh2_base(sources["gh2"], base_gen)
        audit["release_ui_assets"] = install_release_ui_assets(
            release_manifest, base_gen
        )
        dlc_root.mkdir(parents=True, exist_ok=True)
        for package in packages:
            audit["packages"].append(
                install_package(package, dlc_root, args.replace_existing)
            )
        audit["post_install_verification"] = [
            validate_package(dlc_root / row["id"])
            for row in audit["packages"]
        ]
        audit["status"] = "complete"
        audit["completed_utc"] = utc_now()
        write_json(audit_path, audit)
        emit_progress(100, "Installation complete and verified.")
        if not args.keep_work:
            remove_tree(run_root, ignore_errors=True)
            remove_tree(work_root / "media", ignore_errors=True)
            remove_tree(work_root / "rb2", ignore_errors=True)
            remove_tree(work_root / "rb2-build", ignore_errors=True)
        print(
            f"DLC_INSTALL_COMPLETE packages={len(audit['packages'])} "
            f"audit={audit_path}"
        )
        return 0
    except BaseException as error:
        audit["status"] = "failed"
        audit["completed_utc"] = utc_now()
        audit["error"] = str(error)
        write_json(audit_path, audit)
        print(f"DLC_INSTALL_FAILED audit={audit_path}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
