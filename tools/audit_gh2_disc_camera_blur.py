"""Inventory all stock GH2 venue CamShot fade_time values from the original disc.

One bounded camera container at a time, decoded by the shared native typed
parser. Scratch data is scoped to TemporaryDirectory; the report is metadata.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import pycdlib
from audit_gh1_camera_source import entry_index, read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    venues = "battle small1 small2 big theatre fest arena stone".split()
    reports = []
    try:
        index = entry_index(disc)
        with tempfile.TemporaryDirectory(prefix="gh2-camera-blur-") as temporary:
            source = Path(temporary) / "camera.milo_ps2"
            for venue in venues:
                member = f"world/{venue}/gen/{venue}.milo_ps2"
                data = read_entry(disc, member, index)
                source.write_bytes(data)
                result = subprocess.run([str(args.inspector.resolve()), str(source)],
                    capture_output=True, check=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                shots = []
                count = None
                for line in result.stdout.decode("utf-8").splitlines():
                    fields = line.split("\t")
                    if fields[0] == "SHOT":
                        shots.append(dict(name=fields[1], anim_rate=int(fields[2]),
                                          fade_time=float(fields[3])))
                    elif fields[0] == "COUNT":
                        count = int(fields[1])
                if not shots or count != len(shots):
                    raise ValueError(f"Incomplete typed camera inventory for {venue}")
                reports.append(dict(venue=venue, member=member,
                    bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                    count=len(shots), positive=sum(s["fade_time"] > 0 for s in shots),
                    negative=sum(s["fade_time"] < 0 for s in shots), shots=shots))
    finally:
        disc.close()
    args.output.write_text(json.dumps(dict(source=str(args.iso.resolve()),
        scope="Authored CamShot fields; not an execution/rendering parity claim",
        venues=reports), indent=2) + "\n", encoding="utf-8")
    for row in reports:
        print(f"{row['venue']}: {row['count']} shots, {row['positive']} positive fade_time")


if __name__ == "__main__":
    main()
