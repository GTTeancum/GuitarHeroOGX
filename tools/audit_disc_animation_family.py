"""Read a bounded animation family directly from an original PS2 disc.

Uses existing typed MILO readers; no game/emulator input and no retained media.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

import pycdlib
from audit_gh1_camera_source import entry_index, read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--entry", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--milo-tool", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    try:
        data = read_entry(disc, args.entry, entry_index(disc))
    finally:
        disc.close()
    def run(command):
        return subprocess.run(list(map(str, command)), capture_output=True,
            check=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)
        ).stdout.decode("utf-8", errors="strict")
    rows = []
    with tempfile.TemporaryDirectory(prefix="ghogx-disc-animation-") as scratch:
        source = Path(scratch) / "source.milo_ps2"
        source.write_bytes(data)
        listing = run([args.milo_tool.resolve(), "list", source])
        for line in listing.splitlines():
            match = re.match(r"\s+(AnimFilter|TransAnim)\s+size=\d+\s+body=\d+\s+(\S+)", line)
            if not match or args.name.lower() not in match[2].lower():
                continue
            body = Path(scratch) / (str(len(rows)) + ".body")
            run([args.milo_tool.resolve(), "extract-entry", source, match[2], "--out", body])
            kind = "animfilter1" if match[1] == "AnimFilter" else "transanim6"
            decoded = run([args.inspector.resolve(), kind, body])
            rows.append(dict(name=match[2], type=match[1],
                body_sha256=hashlib.sha256(body.read_bytes()).hexdigest(),
                typed_output=decoded.splitlines()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(source=str(args.iso.resolve()),
        entry=args.entry, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
        scope="authored original-disc fields, not execution parity", objects=rows),
        indent=2) + "\n", encoding="utf-8")
    for row in rows:
        print(row["name"] + ": " + row["typed_output"][0])
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
