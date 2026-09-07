"""Locate source symbols in PS2 ARK-v3 disc DTBs without extracting an ARK."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import pycdlib
from audit_gh1_camera_source import entry_index, read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--dtb-tool", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--symbol", action="append",
                        help="Symbol substring to inspect; repeatable (default cam_filter)")
    parser.add_argument("--path", action="append",
                        help="Limit to an exact ARK path; repeatable")
    parser.add_argument("--after", type=int, default=4, help="Context lines after match (1..80)")
    parser.add_argument("--before", type=int, default=2, help="Context lines before match (0..80)")
    args = parser.parse_args()
    if not 1 <= args.after <= 80:
        parser.error("--after must be between 1 and 80")
    if not 0 <= args.before <= 80:
        parser.error("--before must be between 0 and 80")
    symbols = args.symbol or ["cam_filter"]
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    results, skipped = [], []
    checked = 0
    try:
        index = entry_index(disc)
        with tempfile.TemporaryDirectory(prefix="gh1-camera-config-") as temporary:
            source = Path(temporary) / "source.dtb"
            for path, _, size in index[1]:
                if not path.endswith(".dtb"):
                    continue
                if args.path and path not in args.path:
                    continue
                if size > 2*1024*1024:
                    skipped.append(path)
                    continue
                data = read_entry(disc, path, index)
                checked += 1
                source.write_bytes(data)
                output = subprocess.run([str(args.dtb_tool.resolve()), "dump", str(source)],
                    capture_output=True, check=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                # Some stock descriptions contain legacy single-byte glyphs.
                # Escape those bytes; the cam_filter symbol is ASCII.
                lines = output.stdout.decode("utf-8", errors="backslashreplace").splitlines()
                excerpts = [lines[max(0,i-args.before):i+args.after] for i,line in enumerate(lines)
                            if any(symbol in line for symbol in symbols)]
                if excerpts:
                    results.append(dict(path=path, sha256=hashlib.sha256(data).hexdigest(), excerpts=excerpts))
    finally:
        disc.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(dtb_checked=checked, skipped=skipped,
        scope="Symbol matches in decoded source DTBs; declarations are not runtime load order",
        symbols=symbols,
        paths=args.path,
        matches=results), indent=2), encoding="utf-8")
    print(f"Checked {checked} DTBs; {len(results)} files match {symbols}; {len(skipped)} over limit")


if __name__ == "__main__":
    main()
