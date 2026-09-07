"""Release-engineering refresh of compiled GH1 cameras, never an installer step.

Prepare and validate a candidate using hard links for unchanged content, then
optionally replace only the seven venue MILOs and content index. The converter
preserves every non-camera object inside each venue MILO. Source discs and
character/texture/animation files are never modified. Full camera visual parity
is a separate gate; this command does not grant or change qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import pycdlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_gh1_camera_source import entry_index, read_entry
from audit_gh2_disc_camshots import parse_shot
from build_gh1_release_package import VENUES
from install_dlc import validate_package


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def publish(candidate, package, paths, validate):
    """Rollback replacements on an exception; leave other hard links untouched."""
    backups = candidate.parent / "rollback"
    backups.mkdir()
    replaced = []
    try:
        for relative in paths:
            destination = package / relative
            backup = backups / relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            os.link(destination, backup)
            os.replace(candidate / relative, destination)
            replaced.append(relative)
        return validate(package)
    except BaseException:
        for relative in reversed(replaced):
            os.replace(backups / relative, package / relative)
        raise


def refresh(package, iso, converter, report_path, inspector, apply=False):
    package, iso, converter = package.resolve(), iso.resolve(), converter.resolve()
    if report_path.resolve().is_relative_to(package):
        raise ValueError("Keep refresh reports outside the shipped package")
    before = validate_package(package)
    if before["id"] != "project.gh1.converted":
        raise ValueError("Only the preconverted GH1 package is in scope")
    manifest_before = (package / "manifest.json").read_bytes()
    index_before = (package / "content-index.json").read_bytes()
    qualification = package / "qualification.json"
    qualification_before = qualification.read_bytes() if qualification.exists() else None
    index = json.loads(index_before)
    indexed = {row["path"]: row for row in index["files"]}
    changed = {f"world/{venue}/gen/{venue}.milo_ps2" for venue in VENUES}
    if not changed.issubset(indexed):
        raise ValueError("Package is missing one or more GH1 venue camera containers")
    # Same filesystem permits atomic replacements and hard-link rollback.
    with tempfile.TemporaryDirectory(prefix="gh1-camera-refresh-", dir=package.parent) as name:
        scratch = Path(name)
        candidate = scratch / package.name
        candidate.mkdir()
        (candidate / "manifest.json").write_bytes(manifest_before)
        # Package fingerprints include ancillary metadata (e.g. qualification),
        # not only indexed content. Preserve it without re-granting approval.
        for original in package.rglob("*"):
            if not original.is_file() or original.is_relative_to(package / "content"):
                continue
            relative = original.relative_to(package)
            if relative.as_posix() in ("manifest.json", "content-index.json"):
                continue
            if original.is_symlink():
                raise ValueError("Ancillary package metadata must not be a symbolic link")
            destination = candidate / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            os.link(original, destination)
        for relative in indexed:
            destination = candidate / "content" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if relative not in changed:
                os.link(package / "content" / relative, destination)
        disc = pycdlib.PyCdlib()
        disc.open(str(iso))
        source_hashes = {}
        try:
            disc_index = entry_index(disc)
            sources = {
                "cam_paths.dtb": "arena/gen/cam_paths.dtb",
                "gh.dtb": "config/gen/gh.dtb",
                "shared.rnd_ps2": "../../system/run/arena/gen/fx.rnd_ps2",
                **{f"{v}-camera.dtb": f"venues/{v[4:]}/gen/camera.dtb" for v in VENUES},
            }
            for local, member in sources.items():
                data = read_entry(disc, member, disc_index)
                (scratch / local).write_bytes(data)
                source_hashes[member] = hashlib.sha256(data).hexdigest()
        finally:
            disc.close()

        def run(arguments):
            result = subprocess.run([str(converter), *map(str, arguments)],
                                    capture_output=True, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                                    timeout=120)
            if result.returncode:
                raise RuntimeError(result.stderr[-4000:])
            return result.stdout.strip()

        def inspect(path):
            result = subprocess.run([str(inspector.resolve()), str(path)], capture_output=True,
                                    text=True, timeout=120,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode:
                raise RuntimeError(result.stderr[-4000:])
            shots = [parse_shot(line.split("\t")) for line in result.stdout.splitlines()
                     if line.startswith("SHOT\t")]
            return dict(shots=len(shots), zero_weight_shots=sum(s["selection_weight"] == 0 for s in shots),
                        unit_weight_shots=sum(s["selection_weight"] == 1 for s in shots))

        run(["convert", scratch / "shared.rnd_ps2", "--name", "gh1_camera_fx",
             "--out", scratch / "shared.milo_ps2", "--manifest", scratch / "shared.tsv"])
        rows = []
        for venue in VENUES:
            relative = f"world/{venue}/gen/{venue}.milo_ps2"
            source = package / "content" / relative
            destination = candidate / "content" / relative
            result = run(["recompile-gh1-cameras", source, scratch / f"{venue}-camera.dtb",
                          scratch / "cam_paths.dtb", source.with_name("campaths.milo_ps2"),
                          scratch / "shared.milo_ps2", scratch / "gh.dtb", "--out", destination])
            previous_audit, updated_audit = inspect(source), inspect(destination)
            record_count = re.search(r"recompiled (\d+) cameras", result)
            if (not record_count or updated_audit["shots"] != int(record_count[1])
                    or updated_audit["unit_weight_shots"] != updated_audit["shots"]):
                raise RuntimeError(f"{venue}: compiled source-uniform camera weight audit failed")
            old_hash = indexed[relative]["sha256"]
            indexed[relative].update(size=destination.stat().st_size, sha256=digest(destination))
            rows.append(dict(venue=venue, path=relative, before_sha256=old_hash,
                             after_sha256=indexed[relative]["sha256"], converter_result=result,
                             before_camera_audit=previous_audit, after_camera_audit=updated_audit))
        (candidate / "content-index.json").write_text(
            json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        verified = validate_package(candidate)
        # Detect concurrent edits before replacing anything in the real package.
        if validate_package(package) != before or (package / "manifest.json").read_bytes() != manifest_before:
            raise RuntimeError("Package changed during camera refresh; nothing applied")
        if (package / "content-index.json").read_bytes() != index_before:
            raise RuntimeError("Content index changed during camera refresh; nothing applied")
        if (qualification.read_bytes() if qualification.exists() else None) != qualification_before:
            raise RuntimeError("Qualification changed during camera refresh; nothing applied")
        if apply:
            verified = publish(candidate, package,
                               ["content/" + p for p in sorted(changed)] + ["content-index.json"],
                               validate_package)
        report = dict(schema_version=1, package=str(package), applied=apply,
                      converter_sha256=digest(converter), source_entry_sha256=source_hashes,
                      inspector_sha256=digest(inspector),
                      before=before, after=verified, cameras=rows,
                      unchanged_content_files=len(indexed) - len(changed),
                      unchanged_content_verified=True, manifest_unchanged=True,
                      qualification_unchanged=(qualification.read_bytes() if qualification.exists() else None) == qualification_before,
                      source_disc_modified=False,
                      scope="Source camera payload refresh, not retail visual-parity qualification")
    report["scratch_removed"] = not scratch.exists()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"cameras={len(rows)} applied={apply} unchanged_files={len(indexed)-len(changed)} "
          f"candidate_validated=True scratch_removed={report['scratch_removed']}")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("package", "iso", "converter", "report", "inspector"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--apply", action="store_true", help="Publish the validated camera-only refresh")
    args = parser.parse_args()
    refresh(args.package, args.iso, args.converter, args.report, args.inspector, args.apply)


if __name__ == "__main__":
    main()
