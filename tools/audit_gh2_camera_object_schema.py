"""Retain retail camera/world authoring schema without extracting the disc tree."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import pycdlib
from audit_gh1_camera_source import read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--dtb-tool", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    member = "../../system/run/world/gen/world_objects.dtb"
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    try:
        data = read_entry(disc, member)
    finally:
        disc.close()
    with tempfile.TemporaryDirectory(prefix="gh2-camera-schema-") as temporary:
        source = Path(temporary) / "schema.dtb"
        source.write_bytes(data)
        result = subprocess.run([str(args.dtb_tool.resolve()), "dump", str(source)],
            capture_output=True, check=True,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    lines = result.stdout.decode("utf-8", errors="backslashreplace").splitlines()
    sections = {}
    for name in ("CamShot", "WorldDir"):
        start = lines.index("   " + name)
        end = next(i for i in range(start, len(lines)) if lines[i] == ")")
        sections[name] = lines[start - 1:end + 1]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"source": member,
        "source_sha256": hashlib.sha256(data).hexdigest(), "sections": sections},
        indent=2), encoding="utf-8")
    print("Retained retail CamShot and WorldDir schema; temporary DTB removed")


if __name__ == "__main__":
    main()
