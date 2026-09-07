"""Inventory every stock GH2 venue CamShot through the native typed parser."""

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import pycdlib

from audit_gh1_camera_source import entry_index, read_entry


FIELDS = (
    "name", "anim_rate", "path_ease", "fade_time", "selection_weight",
    "path", "category", "keyframes", "target_frames", "parent_frames",
    "parent_rotation_frames", "same_target_pairs", "different_target_pairs",
    "target_appears_pairs", "target_disappears_pairs", "no_target_pairs", "looping",
)


def parse_shot(fields):
    if len(fields) != len(FIELDS) + 1 or fields[0] != "SHOT":
        raise ValueError("Malformed native CamShot audit row")
    row = dict(zip(FIELDS, fields[1:]))
    for key in (
        "anim_rate", "keyframes", "target_frames", "parent_frames",
        "parent_rotation_frames", "same_target_pairs",
        "different_target_pairs", "target_appears_pairs",
        "target_disappears_pairs", "no_target_pairs", "looping",
    ):
        row[key] = int(row[key])
    for key in ("path_ease", "fade_time", "selection_weight"):
        row[key] = float(row[key])
    for key in ("path", "category"):
        if row[key] == "-":
            row[key] = ""
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    venue_names = "battle small1 small2 big theatre fest arena stone".split()
    venues = []
    try:
        index = entry_index(disc)
        with tempfile.TemporaryDirectory(prefix="gh2-camshot-audit-") as temporary:
            source = Path(temporary) / "world.milo_ps2"
            for venue_name in venue_names:
                member = f"world/{venue_name}/gen/{venue_name}.milo_ps2"
                data = read_entry(disc, member, index)
                source.write_bytes(data)
                result = subprocess.run(
                    [str(args.inspector.resolve()), str(source)],
                    capture_output=True, check=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                shots = []
                count = None
                for line in result.stdout.decode("utf-8").splitlines():
                    fields = line.split("\t")
                    if fields[0] == "SHOT":
                        shots.append(parse_shot(fields))
                    elif fields[0] == "FRAME":
                        if not shots or shots[-1]["name"] != fields[1] or len(fields) < 24:
                            raise ValueError("Malformed native CamShot frame row")
                        shots[-1].setdefault("frames", []).append(dict(
                            index=int(fields[2]), duration=float(fields[3]),
                            blend=float(fields[4]), blend_ease=float(fields[5]),
                            fov=float(fields[6]), screen_offset=list(map(float, fields[7:9])),
                            world_offset=list(map(float, fields[9:21])),
                            parent=fields[21], parent_part=fields[22],
                            parent_rotation=bool(int(fields[23])), targets=fields[24:]))
                    elif fields[0] == "HIDE":
                        if (not shots or shots[-1]["name"] != fields[1]
                                or len(fields) != 4):
                            raise ValueError("Malformed native CamShot hide row")
                        hide_list = shots[-1].setdefault("hide_list", [])
                        if int(fields[2]) != len(hide_list):
                            raise ValueError("Out-of-order native CamShot hide row")
                        hide_list.append(fields[3])
                    elif fields[0] == "COUNT":
                        count = int(fields[1])
                if not shots or count != len(shots):
                    raise ValueError(
                        f"Incomplete typed CamShot inventory for {venue_name}"
                    )
                venues.append({
                    "venue": venue_name, "member": member,
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "camshot_count": len(shots),
                    "path_shot_count": sum(bool(s["path"]) for s in shots),
                    "parent_shot_count": sum(s["parent_frames"] > 0 for s in shots),
                    "target_shot_count": sum(s["target_frames"] > 0 for s in shots),
                    "shots": shots,
                })
    finally:
        disc.close()

    totals = {
        "venues": len(venues),
        "camshots": sum(v["camshot_count"] for v in venues),
        "path_shots": sum(v["path_shot_count"] for v in venues),
        "parented_shots": sum(v["parent_shot_count"] for v in venues),
        "targeted_shots": sum(v["target_shot_count"] for v in venues),
    }
    report = {
        "source": str(args.iso.resolve()),
        "scope": (
            "Typed authored GH2 USA CamShot data. Symbols establish serialized "
            "driver branches; resolved live pointers still require runtime proof."
        ),
        "totals": totals,
        "venues": venues,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(totals, sort_keys=True))
    for venue in venues:
        print(
            f"{venue['venue']}: {venue['camshot_count']} shots, "
            f"{venue['path_shot_count']} path, "
            f"{venue['parent_shot_count']} parent, "
            f"{venue['target_shot_count']} target"
        )


if __name__ == "__main__":
    main()
