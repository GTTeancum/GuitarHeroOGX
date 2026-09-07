"""Verify native intro animation polling against a retained authored fixture."""
import argparse
import json
import re
from pathlib import Path


def audit(source, capture):
    mesh = source["mesh"]
    events = source["events"]
    keys = source["translation_keys"]
    samples = []
    for line in capture.get("animation_samples", []):
        if not line.startswith("[world] venue AnimFilter sample ") or f"mesh={mesh} " not in line:
            continue
        match = re.search(r"event=(\S+) filter=(\S+).*?frame=([-\d.]+).*?value=\(([^)]+)\).*? blend=([-\d.]+)", line)
        if not match:
            continue
        event, name, frame, value, blend = match.groups()
        frame, blend = float(frame), float(blend)
        actual = [float(v) for v in value.split()]
        t = (frame-keys[0]["frame"])/(keys[1]["frame"]-keys[0]["frame"])
        expected = [a+(b-a)*t for a,b in zip(keys[0]["value"],keys[1]["value"])]
        samples.append(dict(event=event, filter=name, frame=frame, blend=blend,
                            position_error=max(abs(a-b) for a,b in zip(actual,expected))))
    rising = [s for s in samples if s["filter"] == events["intro_start"]["filter"]]
    ending = [s for s in samples if s["filter"] == events["intro_end"]["filter"]]
    checks = dict(
        authored_translation=bool(samples) and max(s["position_error"] for s in samples) < 0.001,
        intro_polled=len(rising) >= 3 and rising[-1]["frame"] > rising[0]["frame"],
        intro_advanced=bool(rising) and max(s["frame"] for s in rising) >= events["intro_start"]["end"]*0.9,
        release_blend_advanced=bool(ending) and max(s["blend"] for s in ending) > 0.5,
        source_end_frame=bool(ending) and all(s["frame"] == events["intro_end"]["end"] for s in ending))
    return dict(checks=checks, passed=all(checks.values()), samples=samples,
                exe_sha256=capture["exe_sha256"], source_sha256=source["source_sha256"],
                scope="Authored fixture and native telemetry, not matched-retail visual parity")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(json.loads(args.fixture.read_text()), json.loads(args.capture.read_text()))
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("PASS" if result["passed"] else "FAIL", result["checks"])
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
