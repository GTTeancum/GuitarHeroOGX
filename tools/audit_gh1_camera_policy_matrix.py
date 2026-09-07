#!/usr/bin/env python3
"""Test migrated GH1 cameras in an isolated hard-linked DLC snapshot.

Only the small camera MILOs and index are copied. Installed assets are never
written. The temporary snapshot is removed after the native hidden run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import pycdlib
from audit_gh1_camera_source import read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("iso", "converter", "exe", "ark-dir", "addons-dir", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--venues", nargs="+", default=["gh1_big_club", "gh1_theatre"])
    parser.add_argument("--frames", type=int, default=181)
    parser.add_argument("--start", type=float, default=40,
                        help="Native song seek time; use 0 for startup coverage")
    parser.add_argument("--show-highway", action="store_true")
    parser.add_argument("--videos", nargs="*", default=[])
    parser.add_argument("--character", default="funk1")
    parser.add_argument("--character-variant")
    parser.add_argument("--shot")
    parser.add_argument("--path-offset", type=float, default=0)
    parser.add_argument("--occlusion-audit", action="store_true")
    parser.add_argument("--visibility-audit", action="store_true")
    parser.add_argument("--source-servo", action="store_true",
                        help="Enable the Character-local persistent source servo integration")
    parser.add_argument("--recompile", action="store_true",
                        help="Resample camera curves from original DTBs, including full shake/path composition")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    try:
        policy = read_entry(disc, "arena/gen/cam_paths.dtb")
        source_cameras = {}
        shared_fx = None
        game_config = None
        if args.recompile:
            game_config = read_entry(disc, "config/gen/gh.dtb")
            shared_fx = read_entry(disc, "../../system/run/arena/gen/fx.rnd_ps2")
            for venue in args.venues:
                if venue.startswith("gh1_"):
                    source_cameras[venue] = read_entry(disc, f"venues/{venue[4:]}/gen/camera.dtb")
    finally:
        disc.close()
    source = args.addons_dir.resolve() / "project.gh1.converted"
    if not (source / "manifest.json").is_file():
        raise ValueError("Missing staged converted GH1 package")
    changed = {f"content/world/{v}/gen/{v}.milo_ps2" for v in args.venues if v.startswith("gh1_")}
    if not changed:
        raise ValueError("Select at least one GH1 venue")
    original_hashes = {p: hashlib.sha256((source / p).read_bytes()).hexdigest() for p in changed}
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    reports = []
    # Same volume is required for hard links; never copy a second DLC tree.
    with tempfile.TemporaryDirectory(prefix="camera-policy-", dir=args.output.resolve()) as temporary:
        scratch = Path(temporary)
        target = scratch / "DLC/project.gh1.converted"
        for path in source.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if relative.as_posix() in changed or relative.as_posix() == "content-index.json":
                continue
            os.link(path, destination)
        policy_path = scratch / "cam_paths.dtb"
        policy_path.write_bytes(policy)
        shared_path = scratch / "shared.milo_ps2"
        if args.recompile:
            game_config_path = scratch / "gh.dtb"
            game_config_path.write_bytes(game_config)
            raw_shared = scratch / "shared.rnd_ps2"
            raw_shared.write_bytes(shared_fx)
            result = subprocess.run([str(args.converter.resolve()), "convert", str(raw_shared),
                                     "--name", "gh1_camera_fx", "--out", str(shared_path),
                                     "--manifest", str(scratch / "shared.tsv")],
                                    capture_output=True, text=True, creationflags=flags)
            if result.returncode:
                raise RuntimeError(result.stderr)
        for relative in sorted(changed):
            if args.recompile:
                venue = Path(relative).stem
                camera_path = scratch / f"{venue}-camera.dtb"
                camera_path.write_bytes(source_cameras[venue])
                command = [str(args.converter.resolve()), "recompile-gh1-cameras",
                           str(source / relative), str(camera_path), str(policy_path),
                           str((source / relative).parent / "campaths.milo_ps2"), str(shared_path), str(game_config_path),
                           "--out", str(target / relative)]
            else:
                command = [str(args.converter.resolve()), "rebind-gh1-camera-paths",
                           str(source / relative), str(policy_path), "--out", str(target / relative)]
            result = subprocess.run(command,
                                    capture_output=True, text=True, creationflags=flags)
            if result.returncode:
                raise RuntimeError(result.stderr)
            reports.append({"path": relative, "result": result.stdout.strip(),
                            "source_sha256": original_hashes[relative],
                            "proof_sha256": hashlib.sha256((target / relative).read_bytes()).hexdigest()})
        index = json.loads((source / "content-index.json").read_text(encoding="utf-8"))
        for item in index["files"]:
            path = "content/" + item["path"]
            if path in changed:
                data = (target / path).read_bytes()
                item.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        (target / "content-index.json").write_text(json.dumps(index), encoding="utf-8")
        command = [sys.executable, str(Path(__file__).with_name("audit_camera_venue_matrix.py")),
                   "--exe", str(args.exe.resolve()), "--ark-dir", str(args.ark_dir.resolve()),
                   "--addons-dir", str(scratch / "DLC"), "--output", str(args.output.resolve()),
                   "--frames", str(args.frames), "--start", str(args.start),
                   "--venues", *args.venues, "--videos", *args.videos]
        command += ["--character", args.character]
        if args.shot:
            command += ["--shot", args.shot, "--path-offset", str(args.path_offset)]
        if args.occlusion_audit:
            command += ["--occlusion-audit"]
        if args.visibility_audit:
            command += ["--visibility-audit"]
        if args.source_servo:
            command += ["--source-servo"]
        if args.show_highway:
            command += ["--show-highway"]
        if args.recompile:
            command += ["--require-gh1-helper"]
        if args.character_variant:
            command += ["--character-variant", args.character_variant]
        result = subprocess.run(command, creationflags=flags)
        if result.returncode:
            raise RuntimeError("Native camera matrix failed")
    for relative, expected in original_hashes.items():
        if hashlib.sha256((source / relative).read_bytes()).hexdigest() != expected:
            raise RuntimeError("Installed camera asset unexpectedly changed")
    (args.output / "migration.json").write_text(json.dumps({
        "mode": "recompile" if args.recompile else "rebind",
        "converter_sha256": hashlib.sha256(args.converter.read_bytes()).hexdigest(),
        "source_policy_sha256": hashlib.sha256(policy).hexdigest(),
        "source_camera_dtb_sha256": {venue: hashlib.sha256(data).hexdigest()
                                     for venue, data in source_cameras.items()},
        "source_shared_fx_sha256": hashlib.sha256(shared_fx).hexdigest() if shared_fx else None,
        "source_game_config_sha256": hashlib.sha256(game_config).hexdigest() if game_config else None,
        "installed_assets_unchanged": True, "temporary_snapshot_removed": True,
        "cameras": reports}, indent=2), encoding="utf-8")
    action = "recompiled" if args.recompile else "migrated"
    print(f"{len(reports)} venue camera files {action} for proof; installed originals unchanged; snapshot removed")


if __name__ == "__main__":
    main()
