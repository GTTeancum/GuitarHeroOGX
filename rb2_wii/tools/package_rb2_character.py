#!/usr/bin/env python3
"""Publish one converted RB2 character as non-replacing loose GHOGX DLC."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

from PIL import Image


def normalized(path: str) -> Path:
    value = path.replace("\\", "/").strip("/")
    if not value or ".." in Path(value).parts:
        raise ValueError(f"unsafe manifest path {path!r}")
    return Path(*value.split("/"))


def encode_portrait(source: Path, crop: tuple[int, int, int, int], output: Path) -> None:
    image = Image.open(source).convert("RGBA").crop(crop)
    image = image.resize((64, 128), Image.Resampling.LANCZOS)
    header = struct.pack("<BBiBHHHH", 1, 32, 3, 0, 64, 128, 256, 0) + bytes(17)
    payload = bytearray()
    for red, green, blue, alpha in image.getdata():
        payload.extend((red, green, blue, min(128, (alpha + 1) // 2)))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + payload)


def validate_package(root: Path, expected_manifest: dict | None = None) -> dict:
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"package manifest does not exist: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if expected_manifest is not None and manifest != expected_manifest:
        raise ValueError("packaged manifest does not match the requested manifest")

    content_root = normalized(manifest.get("content_root", "content"))
    content = root / content_root
    declared = {normalized(row).as_posix() for row in manifest.get("files", [])}
    index_name = normalized(manifest.get("content_index", "content-index.json"))
    index_path = root / index_name
    if not index_path.is_file():
        raise ValueError(f"package content index does not exist: {index_path}")
    index = json.loads(index_path.read_text(encoding="utf-8"))
    if index.get("package_id") != manifest.get("id"):
        raise ValueError("content index package_id does not match manifest id")

    indexed_rows = index.get("files", [])
    indexed_paths = [normalized(row["path"]).as_posix() for row in indexed_rows]
    if len(indexed_paths) != len(set(indexed_paths)):
        raise ValueError("content index contains duplicate paths")
    if set(indexed_paths) != declared:
        raise ValueError("content index paths do not exactly match manifest files")

    actual_paths = {
        path.relative_to(content).as_posix()
        for path in content.rglob("*")
        if path.is_file()
    } if content.is_dir() else set()
    if actual_paths != declared:
        missing = sorted(declared - actual_paths)
        extra = sorted(actual_paths - declared)
        raise ValueError(
            f"package content tree mismatch: missing={missing} extra={extra}"
        )

    for row, relative in zip(indexed_rows, indexed_paths):
        data = (content / normalized(relative)).read_bytes()
        actual_size = len(data)
        actual_hash = hashlib.sha256(data).hexdigest()
        if row.get("size") != actual_size or row.get("sha256") != actual_hash:
            raise ValueError(
                f"package payload does not match content index: {relative} "
                f"expected_size={row.get('size')} actual_size={actual_size} "
                f"expected_sha256={row.get('sha256')} actual_sha256={actual_hash}"
            )
    return index


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--portrait-source", type=Path)
    parser.add_argument("--portrait-crop", metavar="L,T,R,B")
    parser.add_argument(
        "--extra-file",
        action="append",
        default=[],
        metavar="SOURCE=MANIFEST_PATH",
        help="copy an additional declared runtime dependency into the package",
    )
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="verify an existing package payload against its manifest and index",
    )
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if args.validate_only:
        index = validate_package(args.out, manifest)
        print(
            f"package={manifest['id']} validated_files={len(index['files'])} "
            f"bytes={sum(row['size'] for row in index['files'])} out={args.out}"
        )
        return 0

    missing_build_args = [
        name
        for name, value in (
            ("--model", args.model),
            ("--portrait-source", args.portrait_source),
            ("--portrait-crop", args.portrait_crop),
        )
        if value is None
    ]
    if missing_build_args:
        parser.error(
            "the following arguments are required unless --validate-only is used: "
            + ", ".join(missing_build_args)
        )
    if manifest.get("replaces"):
        raise ValueError("release RB2 character package must be additive")
    characters = manifest.get("characters", [])
    if len(characters) != 1 or len(characters[0].get("outfits", [])) != 1:
        raise ValueError("RB2 package template must declare one character/outfit")
    model_path = normalized(characters[0]["outfits"][0]["model"])
    portrait_path = normalized(characters[0]["portrait"])
    declared = {normalized(row).as_posix() for row in manifest.get("files", [])}
    extra_files: dict[str, Path] = {}
    for value in args.extra_file:
        if "=" not in value:
            raise ValueError("--extra-file must be SOURCE=MANIFEST_PATH")
        source_value, target_value = value.split("=", 1)
        target = normalized(target_value).as_posix()
        if target in extra_files:
            raise ValueError(f"duplicate extra-file target {target}")
        source = Path(source_value)
        if not source.is_file():
            raise ValueError(f"extra-file source does not exist: {source}")
        extra_files[target] = source
    expected = {model_path.as_posix(), portrait_path.as_posix(), *extra_files}
    if expected != declared:
        raise ValueError(
            "manifest files must exactly name model, portrait, and extra-file targets"
        )
    crop_values = tuple(int(value.strip()) for value in args.portrait_crop.split(","))
    if len(crop_values) != 4:
        raise ValueError("portrait crop must be L,T,R,B")

    content = args.out / manifest.get("content_root", "content")
    target_model = content / model_path
    target_model.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.model, target_model)
    encode_portrait(args.portrait_source, crop_values, content / portrait_path)
    for relative, source in extra_files.items():
        target = content / normalized(relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    index = {"schema_version": 1, "package_id": manifest["id"], "files": []}
    for relative in sorted(declared):
        path = content / normalized(relative)
        data = path.read_bytes()
        index["files"].append({
            "path": relative,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    index_name = manifest.get("content_index", "content-index.json")
    (args.out / index_name).write_text(
        json.dumps(index, indent=2) + "\n", encoding="utf-8"
    )
    validate_package(args.out, manifest)
    print(
        f"package={manifest['id']} files={len(declared)} "
        f"bytes={sum(row['size'] for row in index['files'])} out={args.out}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
