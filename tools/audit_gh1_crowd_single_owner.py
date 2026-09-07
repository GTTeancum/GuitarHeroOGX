"""Verify GH1 duplicate-draw removal leaves every submitted camera unchanged."""
import argparse
import json
from pathlib import Path
import re

VENUES = "basement small_club small_club_multi big_club theatre fest arena".split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for venue in VENUES:
        name = "gh1_" + venue
        before = json.loads((args.before / name / "camera.json").read_text())
        after = json.loads((args.after / name / "camera.json").read_text())
        a, b = (row["camera_transform_samples"] for row in (before, after))
        rows = after["crowd_regions"]
        owners = [line for line in rows if " single_owner=" in line]
        duplicate_count = sum(int(re.search(r"duplicate_draws=(\d+)", line)[1])
                              for line in owners)
        lighting_count = sum(int(re.search(r"children=(\d+)", line)[1])
                             for line in rows if "scene=lighting owner=" in line)
        errors = []
        if after["exe_sha256"] != args.exe_sha256.lower():
            errors.append("wrong native executable")
        if not after["smoke_pass"] or after["exit_code"] != 0:
            errors.append("native smoke failure")
        if len(a) != 181 or a != b:
            errors.append("camera sequence differs or incomplete")
        if not any(f"scene={name} owner=" in line for line in rows):
            errors.append("missing primary ownership telemetry")
        if duplicate_count != lighting_count:
            errors.append("not every secondary crowd object has an owner")
        results.append(dict(venue=name, camera_frames=len(b),
                            camera_sequence_identical=a == b,
                            secondary_crowd_objects=lighting_count,
                            duplicate_draws_removed=duplicate_count,
                            owner_evidence=owners, errors=errors))
    report = dict(exe_sha256=args.exe_sha256.lower(),
                  scope="draw ownership and exact camera invariance; not retail visual parity",
                  before=str(args.before.resolve()), after=str(args.after.resolve()),
                  pass_all=all(not row["errors"] for row in results), venues=results)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    for row in results:
        print(f"{row['venue']}: identical={row['camera_sequence_identical']} "
              f"removed={row['duplicate_draws_removed']} errors={row['errors']}")
    return 0 if report["pass_all"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
