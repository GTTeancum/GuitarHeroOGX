#!/usr/bin/env python3
"""Probe GH3 PS2 assets with an external NXTools checkout."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import types
from dataclasses import asdict, dataclass
from pathlib import Path

import gh3_datap


MIDORI_BASE_SKINS = [
    "models/guitarists/midori_1.skin.ps2",
    "models/guitarists/midori_2.skin.ps2",
]
MIDORI_BASE_TEX = [
    "models/guitarists/midori_1.tex.ps2",
    "models/guitarists/midori_2.tex.ps2",
]


@dataclass(frozen=True)
class SkinProbe:
    path: str
    is_ps2_skin: bool
    is_gh5_layout: bool
    mesh_count: int | None = None
    material_entry_count: int | None = None
    error: str | None = None


@dataclass(frozen=True)
class TextureProbe:
    path: str
    texture_count: int | None = None
    tbp_cbp_map_count: int | None = None
    textures: list[dict[str, int | str]] | None = None
    error: str | None = None


def load_nxtools_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def load_nxtools(nxtools_root: Path):
    package = types.ModuleType("nxtools")
    package.__path__ = [str(nxtools_root)]
    sys.modules["nxtools"] = package

    helpers = types.ModuleType("nxtools.helpers")
    helpers.FromGHWTCoords = lambda value: value
    sys.modules["nxtools.helpers"] = helpers

    return (
        load_nxtools_module("nxtools.ps2_skin", nxtools_root / "ps2_skin.py"),
        load_nxtools_module("nxtools.ps2_tex", nxtools_root / "ps2_tex.py"),
    )


def entries_by_path(entries: list[gh3_datap.DatapEntry]) -> dict[str, gh3_datap.DatapEntry]:
    return {entry.normalized_path: entry for entry in entries}


def probe_skin(ps2_skin, path: str, data: bytes) -> SkinProbe:
    is_ps2_skin = bool(ps2_skin.IsPS2Skin(data))
    is_gh5_layout = bool(ps2_skin.IsPS2SkinGH5(data))
    try:
        scene = ps2_skin.ParsePS2Skin(data)
    except Exception as exc:
        return SkinProbe(
            path=path,
            is_ps2_skin=is_ps2_skin,
            is_gh5_layout=is_gh5_layout,
            error=f"{type(exc).__name__}: {exc}",
        )
    return SkinProbe(
        path=path,
        is_ps2_skin=is_ps2_skin,
        is_gh5_layout=is_gh5_layout,
        mesh_count=len(scene.meshes),
        material_entry_count=len(scene.entries),
    )


def probe_texture(ps2_tex, path: str, data: bytes) -> TextureProbe:
    try:
        textures = ps2_tex.ParsePS2SceneTex(data)
        tbp_cbp_map = ps2_tex.BuildTBPCBPMap(data)
    except Exception as exc:
        return TextureProbe(path=path, error=f"{type(exc).__name__}: {exc}")
    return TextureProbe(
        path=path,
        texture_count=len(textures),
        tbp_cbp_map_count=len(tbp_cbp_map),
        textures=[
            {
                "checksum": f"0x{tex.checksum:08x}",
                "width": tex.width,
                "height": tex.height,
                "psm": ps2_tex.DescribePSM(tex.psm),
            }
            for tex in textures
        ],
    )


def command_midori(args: argparse.Namespace) -> int:
    nxtools_root = args.nxtools.resolve()
    ps2_skin, ps2_tex = load_nxtools(nxtools_root)
    hed, wad, entries = gh3_datap.load_entries(args.iso)
    by_path = entries_by_path(entries)

    skins: list[SkinProbe] = []
    for path in MIDORI_BASE_SKINS:
        entry = by_path[path]
        data = gh3_datap.read_datap_entry(args.iso, wad, entry)
        skins.append(probe_skin(ps2_skin, path, data))

    textures: list[TextureProbe] = []
    for path in MIDORI_BASE_TEX:
        entry = by_path[path]
        data = gh3_datap.read_datap_entry(args.iso, wad, entry)
        textures.append(probe_texture(ps2_tex, path, data))

    payload = {
        "iso": str(args.iso),
        "nxtools": str(nxtools_root),
        "hed": asdict(hed),
        "wad": asdict(wad),
        "skins": [asdict(item) for item in skins],
        "textures": [asdict(item) for item in textures],
    }
    text = json.dumps(payload, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=Path("Guitar Hero III - Legends of Rock (USA).iso"))
    parser.add_argument("--nxtools", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.set_defaults(func=command_midori)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.iso.exists():
        raise SystemExit(f"ISO not found: {args.iso}")
    if not (args.nxtools / "ps2_skin.py").exists():
        raise SystemExit(f"NXTools checkout missing ps2_skin.py: {args.nxtools}")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
